#include <horizon/io.h>
#include <horizon/hal.h>
#include <horizon/console.h>
#include <horizon/status.h>

/* ---------------- Console0 Driver ---------------- */
static NTSTATUS console_dispatch(device_object_t *dev, hnt_irp_t *irp) {
    UNUSED(dev);
    switch (irp->major) {
        case IRP_MJ_CREATE:
        case IRP_MJ_CLOSE:
            return STATUS_SUCCESS;
        case IRP_MJ_WRITE: {
            const char *p = (const char *)irp->buffer;
            for (size_t i = 0; i < irp->length; i++) {
                console_putc(p[i]);
            }
            irp->information = irp->length;
            return STATUS_SUCCESS;
        }
        case IRP_MJ_READ: {
            char *p = (char *)irp->buffer;
            size_t i = 0;
            while (i < irp->length) {
                char c = console_getc_blocking();
                p[i++] = c;
                if (c == '\n') break;
            }
            irp->information = i;
            return STATUS_SUCCESS;
        }
        default:
            return STATUS_INVALID_DEVICE_REQUEST;
    }
}

/* ---------------- Serial0 Driver ---------------- */
static NTSTATUS serial_dispatch(device_object_t *dev, hnt_irp_t *irp) {
    UNUSED(dev);
    switch (irp->major) {
        case IRP_MJ_CREATE:
        case IRP_MJ_CLOSE:
            return STATUS_SUCCESS;
        case IRP_MJ_WRITE: {
            const char *p = (const char *)irp->buffer;
            for (size_t i = 0; i < irp->length; i++) {
                serial_putc(p[i]);
            }
            irp->information = irp->length;
            return STATUS_SUCCESS;
        }
        case IRP_MJ_READ: {
            char *p = (char *)irp->buffer;
            size_t i = 0;
            while (i < irp->length) {
                int c = serial_getc_nonblock();
                if (c < 0) break;
                p[i++] = (char)c;
            }
            irp->information = i;
            return STATUS_SUCCESS;
        }
        default:
            return STATUS_INVALID_DEVICE_REQUEST;
    }
}

/* ---------------- Null Driver ---------------- */
static NTSTATUS null_dispatch(device_object_t *dev, hnt_irp_t *irp) {
    UNUSED(dev);
    switch (irp->major) {
        case IRP_MJ_CREATE:
        case IRP_MJ_CLOSE:
            return STATUS_SUCCESS;
        case IRP_MJ_READ:
            irp->information = 0;
            return STATUS_END_OF_FILE;
        case IRP_MJ_WRITE:
            irp->information = irp->length;
            return STATUS_SUCCESS;
        default:
            return STATUS_INVALID_DEVICE_REQUEST;
    }
}

static driver_object_t g_console_drv = {
    .name = "ConsoleDrv",
    .dispatch = {
        [IRP_MJ_CREATE] = console_dispatch,
        [IRP_MJ_CLOSE]  = console_dispatch,
        [IRP_MJ_READ]   = console_dispatch,
        [IRP_MJ_WRITE]  = console_dispatch,
    }
};

static driver_object_t g_serial_drv = {
    .name = "SerialDrv",
    .dispatch = {
        [IRP_MJ_CREATE] = serial_dispatch,
        [IRP_MJ_CLOSE]  = serial_dispatch,
        [IRP_MJ_READ]   = serial_dispatch,
        [IRP_MJ_WRITE]  = serial_dispatch,
    }
};

static driver_object_t g_null_drv = {
    .name = "NullDrv",
    .dispatch = {
        [IRP_MJ_CREATE] = null_dispatch,
        [IRP_MJ_CLOSE]  = null_dispatch,
        [IRP_MJ_READ]   = null_dispatch,
        [IRP_MJ_WRITE]  = null_dispatch,
    }
};

void devices_init(void) {
    device_object_t *dev = NULL;

    KASSERT(NT_SUCCESS(io_register_driver(&g_console_drv)));
    KASSERT(NT_SUCCESS(io_create_device(&g_console_drv, "\\Device\\Console0", NULL, &dev)));

    KASSERT(NT_SUCCESS(io_register_driver(&g_serial_drv)));
    KASSERT(NT_SUCCESS(io_create_device(&g_serial_drv, "\\Device\\Serial0", NULL, &dev)));

    KASSERT(NT_SUCCESS(io_register_driver(&g_null_drv)));
    KASSERT(NT_SUCCESS(io_create_device(&g_null_drv, "\\Device\\Null", NULL, &dev)));
}
