// iOS does not expose the USB/HID passthrough facilities used by RPCS3.
// These libusb-compatible entry points deliberately report an empty bus and
// fail all device operations without pulling a private framework into the app.

#include <stdlib.h>
#include <string.h>

#include <libusb.h>

int LIBUSB_CALL libusb_init_context(libusb_context** context,
	const struct libusb_init_option options[], int option_count)
{
	(void)options; (void)option_count;
	if (context) *context = NULL;
	return LIBUSB_SUCCESS;
}

void LIBUSB_CALL libusb_exit(libusb_context* context) { (void)context; }
int LIBUSB_CALL libusb_has_capability(uint32_t capability) { (void)capability; return 0; }
const char* LIBUSB_CALL libusb_error_name(int error_code)
{
	(void)error_code;
	return "LIBUSB_ERROR_NOT_SUPPORTED";
}

ssize_t LIBUSB_CALL libusb_get_device_list(libusb_context* context, libusb_device*** list)
{
	(void)context;
	if (list) *list = NULL;
	return 0;
}
void LIBUSB_CALL libusb_free_device_list(libusb_device** list, int unref_devices)
{
	(void)list; (void)unref_devices;
}
libusb_device* LIBUSB_CALL libusb_ref_device(libusb_device* device) { return device; }
void LIBUSB_CALL libusb_unref_device(libusb_device* device) { (void)device; }

int LIBUSB_CALL libusb_get_configuration(libusb_device_handle* device, int* configuration)
{
	(void)device;
	if (configuration) *configuration = 0;
	return LIBUSB_ERROR_NO_DEVICE;
}
int LIBUSB_CALL libusb_get_device_descriptor(libusb_device* device,
	struct libusb_device_descriptor* descriptor)
{
	(void)device;
	if (descriptor) memset(descriptor, 0, sizeof(*descriptor));
	return LIBUSB_ERROR_NO_DEVICE;
}
uint8_t LIBUSB_CALL libusb_get_device_address(libusb_device* device) { (void)device; return 0; }
uint8_t LIBUSB_CALL libusb_get_port_number(libusb_device* device) { (void)device; return 0; }

int LIBUSB_CALL libusb_open(libusb_device* device, libusb_device_handle** handle)
{
	(void)device;
	if (handle) *handle = NULL;
	return LIBUSB_ERROR_NOT_SUPPORTED;
}
void LIBUSB_CALL libusb_close(libusb_device_handle* handle) { (void)handle; }
int LIBUSB_CALL libusb_set_configuration(libusb_device_handle* handle, int configuration)
{
	(void)handle; (void)configuration;
	return LIBUSB_ERROR_NOT_SUPPORTED;
}
int LIBUSB_CALL libusb_claim_interface(libusb_device_handle* handle, int interface_number)
{
	(void)handle; (void)interface_number;
	return LIBUSB_ERROR_NOT_SUPPORTED;
}
int LIBUSB_CALL libusb_release_interface(libusb_device_handle* handle, int interface_number)
{
	(void)handle; (void)interface_number;
	return LIBUSB_ERROR_NOT_SUPPORTED;
}
int LIBUSB_CALL libusb_control_transfer(libusb_device_handle* handle, uint8_t request_type,
	uint8_t request, uint16_t value, uint16_t index, unsigned char* data,
	uint16_t length, unsigned int timeout)
{
	(void)handle; (void)request_type; (void)request; (void)value; (void)index;
	(void)data; (void)length; (void)timeout;
	return LIBUSB_ERROR_NOT_SUPPORTED;
}

struct libusb_transfer* LIBUSB_CALL libusb_alloc_transfer(int iso_packets)
{
	if (iso_packets < 0) return NULL;
	const size_t size = sizeof(struct libusb_transfer) +
		(size_t)iso_packets * sizeof(struct libusb_iso_packet_descriptor);
	struct libusb_transfer* transfer = calloc(1, size);
	if (transfer) transfer->num_iso_packets = iso_packets;
	return transfer;
}
int LIBUSB_CALL libusb_submit_transfer(struct libusb_transfer* transfer)
{
	(void)transfer;
	return LIBUSB_ERROR_NOT_SUPPORTED;
}
void LIBUSB_CALL libusb_free_transfer(struct libusb_transfer* transfer) { free(transfer); }

int LIBUSB_CALL libusb_handle_events_timeout_completed(libusb_context* context,
	struct timeval* timeout, int* completed)
{
	(void)context; (void)timeout;
	if (completed) *completed = 1;
	return LIBUSB_SUCCESS;
}

int LIBUSB_CALL libusb_hotplug_register_callback(libusb_context* context,
	int events, int flags, int vendor_id, int product_id, int device_class,
	libusb_hotplug_callback_fn callback, void* user_data,
	libusb_hotplug_callback_handle* handle)
{
	(void)context; (void)events; (void)flags; (void)vendor_id; (void)product_id;
	(void)device_class; (void)callback; (void)user_data;
	if (handle) *handle = 0;
	return LIBUSB_ERROR_NOT_SUPPORTED;
}
void LIBUSB_CALL libusb_hotplug_deregister_callback(libusb_context* context,
	libusb_hotplug_callback_handle handle)
{
	(void)context; (void)handle;
}
