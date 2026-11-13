#pragma once

#include <horizon/types.h>
#include <horizon/ob.h>

typedef enum {
    IRP_MJ_CREATE = 0,
    IRP_MJ_CLOSE,
    IRP_MJ_READ,
    IRP_MJ_WRITE,
    IRP_MJ_DEVICE_CONTROL,
    IRP_MJ_MAX
} irp_major_t;

struct device_object;
struct driver_object;

typedef struct {
    irp_major_t           major;
    struct device_object *device;
    void                 *buffer;
    size_t                length;
    uint64_t              offset;
    uint32_t              ioctl_code;
    NTSTATUS              status;
    size_t                information; /* bytes read/written */
} hnt_irp_t;

typedef NTSTATUS (*driver_dispatch_t)(struct device_object *dev, hnt_irp_t *irp);

typedef struct driver_object {
    char              name[32];
    driver_dispatch_t dispatch[IRP_MJ_MAX];
} driver_object_t;

typedef struct device_object {
    driver_object_t *driver;
    char             name[64];
    void            *dev_ext;
} device_object_t;

typedef struct {
    device_object_t *device;
    void            *fs_node;
    uint64_t         offset;
    ACCESS_MASK      access;
} file_object_t;

void     io_init(void);
NTSTATUS io_register_driver(driver_object_t *drv);
NTSTATUS io_create_device(driver_object_t *drv, const char *ob_path, void *dev_ext, device_object_t **out_dev);
NTSTATUS io_call_driver(device_object_t *dev, hnt_irp_t *irp);

/* File/Device convenience built on OB + IRP */
NTSTATUS io_open_file(const char *path, ACCESS_MASK access, HANDLE *out_handle);
NTSTATUS io_read_file(HANDLE handle, void *buf, size_t len, size_t *out_read);
NTSTATUS io_write_file(HANDLE handle, const void *buf, size_t len, size_t *out_written);
NTSTATUS io_close_file(HANDLE handle);

void     devices_init(void);
