#include <horizon/io.h>
#include <horizon/fs.h>
#include <horizon/ps.h>
#include <horizon/status.h>
#include <horizon/string.h>
#include <horizon/console.h>

static void file_obj_on_delete(void *body) {
    file_object_t *fo = (file_object_t *)body;
    if (fo->device) {
        hnt_irp_t irp = {
            .major = IRP_MJ_CLOSE,
            .device = fo->device,
        };
        io_call_driver(fo->device, &irp);
        ob_deref_object(fo->device);
        fo->device = NULL;
    }
}

void io_init(void) {
    ob_type_t dev_type = {
        .id = OBJ_TYPE_DEVICE,
        .name = "Device",
        .body_size = sizeof(device_object_t),
        .on_close = NULL,
        .on_delete = NULL,
    };
    ob_type_t file_type = {
        .id = OBJ_TYPE_FILE,
        .name = "File",
        .body_size = sizeof(file_object_t),
        .on_close = NULL,
        .on_delete = file_obj_on_delete,
    };
    ob_register_type(&dev_type);
    ob_register_type(&file_type);
}

NTSTATUS io_register_driver(driver_object_t *drv) {
    if (!drv || drv->name[0] == '\0') return STATUS_INVALID_PARAMETER;
    char path[96];
    snprintf(path, sizeof(path), "\\Driver\\%s", drv->name);
    void *obj = NULL;
    NTSTATUS st = ob_create_object(OBJ_TYPE_DIRECTORY, 0, path, &obj);
    return st;
}

NTSTATUS io_create_device(driver_object_t *drv, const char *ob_path, void *dev_ext, device_object_t **out_dev) {
    if (!drv || !ob_path || !out_dev) return STATUS_INVALID_PARAMETER;
    void *body = NULL;
    NTSTATUS st = ob_create_object(OBJ_TYPE_DEVICE, 0, ob_path, &body);
    if (!NT_SUCCESS(st)) return st;

    device_object_t *dev = (device_object_t *)body;
    dev->driver  = drv;
    dev->dev_ext = dev_ext;
    strncpy(dev->name, ob_path, sizeof(dev->name) - 1);

    *out_dev = dev;
    return STATUS_SUCCESS;
}

NTSTATUS io_call_driver(device_object_t *dev, hnt_irp_t *irp) {
    if (!dev || !irp || !dev->driver || irp->major >= IRP_MJ_MAX) {
        return STATUS_INVALID_PARAMETER;
    }
    driver_dispatch_t fn = dev->driver->dispatch[irp->major];
    if (!fn) {
        irp->status = STATUS_INVALID_DEVICE_REQUEST;
        return STATUS_INVALID_DEVICE_REQUEST;
    }
    irp->device = dev;
    irp->status = fn(dev, irp);
    return irp->status;
}

NTSTATUS io_open_file(const char *path, ACCESS_MASK access, HANDLE *out_handle) {
    if (!path || !out_handle) return STATUS_INVALID_PARAMETER;
    kprocess_t *proc = ps_current_process();
    if (!proc) return STATUS_UNSUCCESSFUL;

    /* Check Object Manager namespace first (e.g. \Device\Console0) */
    void *dev_body = NULL;
    if (NT_SUCCESS(ob_lookup_object(path, OBJ_TYPE_DEVICE, &dev_body))) {
        device_object_t *dev = (device_object_t *)dev_body;
        hnt_irp_t irp = { .major = IRP_MJ_CREATE, .device = dev };
        NTSTATUS st = io_call_driver(dev, &irp);
        if (!NT_SUCCESS(st) && st != STATUS_INVALID_DEVICE_REQUEST) {
            ob_deref_object(dev_body);
            return st;
        }

        void *fbody = NULL;
        st = ob_create_object(OBJ_TYPE_FILE, 0, NULL, &fbody);
        if (!NT_SUCCESS(st)) {
            ob_deref_object(dev_body);
            return st;
        }
        file_object_t *fo = (file_object_t *)fbody;
        fo->device = dev; /* transfers ref from ob_lookup_object */
        fo->access = access;

        st = ob_open_handle(&proc->handles, fbody, access, out_handle);
        ob_deref_object(fbody);
        return st;
    }

    /* Fall back to VFS / RamFS */
    fs_node_t *node = fs_lookup(path);
    if (!node && (access & GENERIC_WRITE)) {
        NTSTATUS cst = fs_create_file(path, FS_NODE_FILE, &node);
        if (!NT_SUCCESS(cst)) return cst;
    }
    if (!node) return STATUS_NO_SUCH_FILE;
    if ((access & GENERIC_WRITE) && node->readonly) return STATUS_ACCESS_DENIED;

    void *fbody = NULL;
    NTSTATUS st = ob_create_object(OBJ_TYPE_FILE, 0, NULL, &fbody);
    if (!NT_SUCCESS(st)) return st;
    file_object_t *fo = (file_object_t *)fbody;
    fo->fs_node = node;
    fo->offset  = 0;
    fo->access  = access;

    st = ob_open_handle(&proc->handles, fbody, access, out_handle);
    ob_deref_object(fbody);
    return st;
}

NTSTATUS io_read_file(HANDLE handle, void *buf, size_t len, size_t *out_read) {
    if (!buf) return STATUS_INVALID_PARAMETER;
    kprocess_t *proc = ps_current_process();
    void *fbody = NULL;
    NTSTATUS st = ob_ref_by_handle(&proc->handles, handle, OBJ_TYPE_FILE, GENERIC_READ, &fbody);
    if (!NT_SUCCESS(st)) return st;

    file_object_t *fo = (file_object_t *)fbody;
    size_t nread = 0;

    if (fo->device) {
        hnt_irp_t irp = {
            .major  = IRP_MJ_READ,
            .device = fo->device,
            .buffer = buf,
            .length = len,
            .offset = fo->offset,
        };
        st = io_call_driver(fo->device, &irp);
        nread = irp.information;
        fo->offset += nread;
    } else if (fo->fs_node) {
        st = fs_read_node((fs_node_t *)fo->fs_node, fo->offset, buf, len, &nread);
        fo->offset += nread;
    } else {
        st = STATUS_INVALID_HANDLE;
    }

    if (out_read) *out_read = nread;
    ob_deref_object(fbody);
    return st;
}

NTSTATUS io_write_file(HANDLE handle, const void *buf, size_t len, size_t *out_written) {
    if (!buf) return STATUS_INVALID_PARAMETER;
    kprocess_t *proc = ps_current_process();
    void *fbody = NULL;
    NTSTATUS st = ob_ref_by_handle(&proc->handles, handle, OBJ_TYPE_FILE, GENERIC_WRITE, &fbody);
    if (!NT_SUCCESS(st)) return st;

    file_object_t *fo = (file_object_t *)fbody;
    size_t nwritten = 0;

    if (fo->device) {
        hnt_irp_t irp = {
            .major  = IRP_MJ_WRITE,
            .device = fo->device,
            .buffer = (void *)buf,
            .length = len,
            .offset = fo->offset,
        };
        st = io_call_driver(fo->device, &irp);
        nwritten = irp.information;
        fo->offset += nwritten;
    } else if (fo->fs_node) {
        st = fs_write_node((fs_node_t *)fo->fs_node, fo->offset, buf, len, &nwritten);
        fo->offset += nwritten;
    } else {
        st = STATUS_INVALID_HANDLE;
    }

    if (out_written) *out_written = nwritten;
    ob_deref_object(fbody);
    return st;
}

NTSTATUS io_close_file(HANDLE handle) {
    kprocess_t *proc = ps_current_process();
    if (!proc) return STATUS_INVALID_HANDLE;
    return ob_close_handle(&proc->handles, handle);
}
