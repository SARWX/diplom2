#include "enumeration.h"
#include "net_mac.h"
#include "common.h"
#include "device_id.h"
#include "cmd_parser.h"
#include "sleep.h"
#include "uart.h"
#include "deca_device_api.h"
#include <string.h>

/** @brief Set to 1 after enumeration_start_master() completes successfully. */
static uint8_t enumeration_complete = 0;
/** @brief Current attempt index within the retry loop in enumeration_start_master(). */
static uint8_t retry_count = 0;
/** @brief Count of CMD_OK responses received during SYNC_WAIT phase (deduplicated). */
static volatile int sync_ok_count = 0;
/** @brief Source addresses that already responded OK — prevents double-counting retransmits. */
static net_addr16_t sync_ok_senders[ENUM_MAX_DEVICES];
/** @brief This device's seq_id as assigned by the master and received in SYNC_LIST. */
static uint8_t own_seq_id = 0;

/*==============================================================================
 * Serialization
 *============================================================================*/

static void serialize_device_list(net_devices_list_t* devices,
				   uint8_t* buffer, uint16_t* len)
{
	uint16_t offset  = 0;
	net_device_t* dev = devices->head;

	buffer[offset++] = ENUM_MAGIC_1;
	buffer[offset++] = ENUM_MAGIC_2;
	buffer[offset++] = devices->total_anchors;

	while (dev && offset + MAC_ADDR_LEN + 2 <= ENUM_MAX_PACKET_SIZE) {
		memcpy(buffer + offset, dev->mac_address, MAC_ADDR_LEN);
		offset += MAC_ADDR_LEN;
		buffer[offset++] = dev->seq_id;
		buffer[offset++] = (uint8_t)dev->device_type;
		dev = dev->next;
	}
	*len = offset;
}

static int deserialize_device_list(net_devices_list_t* devices,
				   const uint8_t* buffer, uint16_t len)
{
	uint16_t offset = 0;

	if (len < 3) return -1;
	if (buffer[offset++] != ENUM_MAGIC_1) return -1;
	if (buffer[offset++] != ENUM_MAGIC_2) return -1;

	uint8_t count = buffer[offset++];
	if (count > ENUM_MAX_DEVICES) return -1;

	for (int i = 0; i < count; i++) {
		if (offset + MAC_ADDR_LEN + 2 > len) break;

		uint8_t mac[MAC_ADDR_LEN];
		memcpy(mac, buffer + offset, MAC_ADDR_LEN);
		offset += MAC_ADDR_LEN;
		uint8_t seq_id = buffer[offset++];
		uint8_t device_type = buffer[offset++];

		if (!net_device_find_by_mac(devices, mac)) {
			net_device_t* device = net_device_create(mac, device_type);
			if (device) {
				device->seq_id = seq_id;  /* preserve master-assigned id before add */
				net_device_add(devices, device);
			}
		}
	}
	return 0;
}

/*==============================================================================
 * SYNC_LIST packet building
 *============================================================================*/

static int send_sync_list(net_devices_list_t* devices, net_addr16_t dst_addr)
{
	uint8_t list_buf[ENUM_MAX_PACKET_SIZE];
	uint16_t list_len;
	serialize_device_list(devices, list_buf, &list_len);

	uint8_t prefix_len = cmd_len(CMD_SYNC_LIST);
	uint8_t packet[16 + ENUM_MAX_PACKET_SIZE];  /* 16 is enough for any cmd prefix */
	memcpy(packet, cmd_str(CMD_SYNC_LIST), prefix_len);
	packet[prefix_len] = ' ';
	memcpy(packet + prefix_len + 1, list_buf, list_len);

	return net_send_to_16bit(dst_addr, packet, prefix_len + 1 + list_len);
}

/*==============================================================================
 * Verification
 *============================================================================*/

static int verify_device_list(net_devices_list_t* local, net_devices_list_t* remote)
{
	/* Every locally-discovered device must appear in remote with the same type.
	   Local list may be incomplete (we might have missed some early responses),
	   so we only check containment, not count equality. */
	net_device_t* dev = local->head;
	while (dev) {
		net_device_t* rdv = net_device_find_by_mac(remote, dev->mac_address);
		if (!rdv || rdv->device_type != dev->device_type)
			return -1;
		dev = dev->next;
	}

	/* This device itself must appear in the master's list (16-bit short address) */
	net_addr16_t own = net_get_src_addr16();
	uint8_t own_mac[MAC_ADDR_LEN] = {own & 0xFF, (own >> 8) & 0xFF, 0, 0, 0, 0};
	if (!net_device_find_by_mac(remote, own_mac))
		return -1;

	return 0;
}

/*==============================================================================
 * Master — enumeration_start_master
 *============================================================================*/

#define ENUM_POLL_MS 20

/* Drain one pending RX frame and dispatch it to enumeration_handle_message. */
static void drain_rx(net_devices_list_t* devices)
{
	net_message_t msg;
	if (!net_rx_poll(&msg))
		return;
	enumeration_handle_message(devices, &msg);
	dwt_rxenable(DWT_START_RX_IMMEDIATE);
}

int enumeration_start_master(net_devices_list_t* devices)
{
	enumeration_complete = 0;

	for (retry_count = 0; retry_count < ENUM_RETRY_MAX; retry_count++) {
		uart_printf("Enumeration attempt %d/%d\r\n", retry_count + 1, ENUM_RETRY_MAX);

		net_devices_clear(devices);

		/* Add master itself first so it always appears in the authoritative list */
		{
			net_addr16_t master_addr = net_get_src_addr16();
			uint8_t master_mac[MAC_ADDR_LEN] = {
				master_addr & 0xFF, (master_addr >> 8) & 0xFF, 0, 0, 0, 0
			};
			net_device_t *self = net_device_create(master_mac, DEVICE_TYPE_MAIN_ANCHOR);
			if (self) {
				net_device_add(devices, self);
				own_seq_id = self->seq_id;
			}
		}

		/* Phase 1: broadcast DISCOVER, poll for responses */
		dwt_forcetrxoff();
		if (net_send_broadcast((const uint8_t*)cmd_str(CMD_DISCOVER),
		                       cmd_size(CMD_DISCOVER)) < 0)
			continue;

		net_state.mode = NET_MODE_ENUMERATION;
		dwt_rxenable(DWT_START_RX_IMMEDIATE);

		for (uint32_t t = 0; t < ENUM_LISTEN_MS; t += ENUM_POLL_MS) {
			sleep_ms(ENUM_POLL_MS);
			drain_rx(devices);
		}

		if (devices->total_anchors <= 1) {
			uart_puts("No devices found, retrying...\r\n");
			continue;
		}

		/* Phase 2: broadcast SYNC_LIST, poll for OK responses */
		net_state.mode = NET_MODE_SYNC_WAIT;
		sync_ok_count  = 0;
		memset(sync_ok_senders, 0, sizeof(sync_ok_senders));

		dwt_forcetrxoff();
		send_sync_list(devices, NET_BROADCAST_ADDR);
		dwt_rxenable(DWT_START_RX_IMMEDIATE);

		for (uint32_t t = 0; t < SYNC_WAIT_MS; t += ENUM_POLL_MS) {
			sleep_ms(ENUM_POLL_MS);
			drain_rx(devices);
			if (sync_ok_count >= devices->total_anchors - 1)
				break;
		}

		net_state.mode = NET_MODE_IDLE;

		if (sync_ok_count >= devices->total_anchors - 1) {
			enumeration_complete = 1;
			devices->initialized = 1;
			uart_puts("Enumeration complete\r\n");
			net_devices_print(devices);
			return 0;
		}

		uart_printf("Only %d/%d confirmed, retrying...\r\n",
		            sync_ok_count, devices->total_anchors - 1);
	}

	uart_puts("Enumeration failed\r\n");
	return -1;
}

/*==============================================================================
 * Slave — message handlers (run in main loop context, never in ISR)
 *============================================================================*/

static void handle_device_response(net_devices_list_t* devices, net_message_t* msg)
{
	if (msg->payload_len < 1) return;

	uint8_t mac[MAC_ADDR_LEN] = {0};
	if (msg->src_is_eui64)
		memcpy(mac, msg->src_eui64.bytes, MAC_ADDR_LEN);
	else {
		mac[0] = msg->src_addr16 & 0xFF;
		mac[1] = (msg->src_addr16 >> 8) & 0xFF;
	}

	device_type_t dtype = (msg->payload[0] == 'A') ? DEVICE_TYPE_ANCHOR : DEVICE_TYPE_TAG;

	if (!net_device_find_by_mac(devices, mac)) {
		net_device_t* device = net_device_create(mac, dtype);
		if (device)
			net_device_add(devices, device);
	}
}

static void handle_discover(net_devices_list_t* devices, net_message_t* msg)
{
	/* Fresh start for this enumeration round */
	net_devices_clear(devices);

	/* Register the master (DISCOVER sender) as the first known device */
	{
		uint8_t master_mac[MAC_ADDR_LEN] = {0};
		if (msg->src_is_eui64)
			memcpy(master_mac, msg->src_eui64.bytes, MAC_ADDR_LEN);
		else {
			master_mac[0] = msg->src_addr16 & 0xFF;
			master_mac[1] = (msg->src_addr16 >> 8) & 0xFF;
		}
		net_device_t *master = net_device_create(master_mac, DEVICE_TYPE_MAIN_ANCHOR);
		if (master)
			net_device_add(devices, master);
	}

	/* Random backoff so devices don't all respond at the same instant */
	sleep_ms(common_rand() % (ENUM_LISTEN_MS / 2));

	const uint8_t* response;
	switch (curr_dev->type) {
	case DEVICE_TYPE_ANCHOR: response = (const uint8_t*)"A"; break;
	case DEVICE_TYPE_TAG:    response = (const uint8_t*)"T"; break;
	default:                 return;
	}

	net_send_broadcast(response, 1);
}

static void handle_sync_list(net_devices_list_t* devices, net_message_t* msg)
{
	net_devices_list_t remote;
	net_devices_init(&remote);

	uint8_t prefix_len = cmd_len(CMD_SYNC_LIST) + 1; /* +1 for the space separator */
	if (msg->payload_len <= prefix_len) goto send_err;

	if (deserialize_device_list(&remote,
	                             msg->payload + prefix_len,
	                             msg->payload_len - prefix_len) != 0)
		goto send_err;

	/* Store own seq_id (master included this device in the list) */
	net_addr16_t own = net_get_src_addr16();
	uint8_t own_mac[MAC_ADDR_LEN] = {own & 0xFF, (own >> 8) & 0xFF, 0, 0, 0, 0};
	net_device_t* self = net_device_find_by_mac(&remote, own_mac);
	if (self)
		own_seq_id = self->seq_id;

	/* Verify local discoveries are consistent with master's authoritative list */
	if (verify_device_list(devices, &remote) != 0)
		goto send_err;

	/* Accept master's list: swap remote into local devices */
	net_devices_clear(devices);
	devices->head          = remote.head;
	devices->total_anchors = remote.total_anchors;
	devices->initialized   = 1;
	remote.head            = NULL; /* prevent double-free */

	/* Staggered response to avoid collisions */
	sleep_ms(common_rand() % (SYNC_WAIT_MS / 2));
	dwt_forcetrxoff();
	if (msg->src_is_eui64)
		net_send_to_64bit(&msg->src_eui64,
		                  (const uint8_t*)cmd_str(CMD_OK), cmd_size(CMD_OK));
	else
		net_send_to_16bit(msg->src_addr16,
		                  (const uint8_t*)cmd_str(CMD_OK), cmd_size(CMD_OK));

	net_state.mode = NET_MODE_IDLE;
	net_devices_clear(&remote);
	return;

send_err:
	net_devices_clear(&remote);
	net_state.mode = NET_MODE_IDLE;

	sleep_ms(common_rand() % (SYNC_WAIT_MS / 2));
	dwt_forcetrxoff();
	if (msg->src_is_eui64)
		net_send_to_64bit(&msg->src_eui64,
		                  (const uint8_t*)cmd_str(CMD_ERR), cmd_size(CMD_ERR));
	else
		net_send_to_16bit(msg->src_addr16,
		                  (const uint8_t*)cmd_str(CMD_ERR), cmd_size(CMD_ERR));
}

static void handle_ok(net_message_t* msg)
{
	if (net_state.mode != NET_MODE_SYNC_WAIT) return;

	/* Deduplicate: ignore a second OK from the same source */
	net_addr16_t src = msg->src_is_eui64 ? 0 : msg->src_addr16;
	for (int i = 0; i < sync_ok_count; i++) {
		if (sync_ok_senders[i] == src) return;
	}
	if (sync_ok_count < ENUM_MAX_DEVICES)
		sync_ok_senders[sync_ok_count++] = src;
}

/*==============================================================================
 * Public dispatch — safe to call only from main loop, never from ISR
 *============================================================================*/

void enumeration_handle_message(net_devices_list_t* devices, net_message_t* msg)
{
	if (!msg || msg->payload_len == 0) return;

	char cmd_buf[MAX_PAYLOAD_SIZE + 1];
	uint16_t plen = msg->payload_len < MAX_PAYLOAD_SIZE ? msg->payload_len : MAX_PAYLOAD_SIZE;
	memcpy(cmd_buf, msg->payload, plen);
	cmd_buf[plen] = '\0';

	cmd_parse_result_t result = cmd_parse(cmd_buf);
	switch (result.code) {
	case CMD_DISCOVER: handle_discover(devices, msg);       break;
	case CMD_SYNC_LIST: handle_sync_list(devices, msg);     break;
	case CMD_OK:        handle_ok(msg);                     break;
	case CMD_ERR:       /* log or ignore */                 break;
	default:
		if (plen >= 1 && (cmd_buf[0] == 'A' || cmd_buf[0] == 'T'))
			handle_device_response(devices, msg);
		break;
	}
}

uint8_t enumeration_is_complete(void)    { return enumeration_complete; }
uint8_t enumeration_get_own_seq_id(void) { return own_seq_id; }