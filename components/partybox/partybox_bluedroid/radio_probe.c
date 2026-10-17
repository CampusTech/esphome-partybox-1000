// Read-only controller diagnostics using the pinned SDK's allocation/ownership.
#include "hci/hci_layer.h"
#include "stack/hcimsgs.h"

extern void partybox_radio_event(uint16_t opcode, uint8_t status, const uint8_t* parameters, uint16_t length);

static void completed(BT_HDR* response, void* context) {
  const uint16_t expected = (uint16_t)(uintptr_t)context;
  if (response != NULL && response->len >= 6) {
    const uint8_t* bytes = response->data + response->offset;
    const uint16_t opcode = bytes[3] | ((uint16_t)bytes[4] << 8);
    if (bytes[0] == 0x0E && bytes[1] >= 4 && bytes[1] + 2 <= response->len && opcode == expected)
      partybox_radio_event(opcode, bytes[5], bytes + 6, bytes[1] - 4);
  }
  // Complete callback owns the event; SDK frees the queued command afterward.
  osi_free(response);
}

static void status_received(uint8_t status, BT_HDR* command, void* context) {
  partybox_radio_event((uint16_t)(uintptr_t)context, status, NULL, 0);
  // SDK inspects metadata after returning. Relinquish command ownership so
  // its Command Status branch frees the linked allocation exactly once.
  hci_cmd_metadata_t* metadata = HCI_GET_CMD_METAMSG(command);
  metadata->command_status_cb = NULL;
}

bool partybox_radio_read(uint16_t opcode) {
  switch (opcode) {
    case 0x0C19:
    case 0x0C23:
    case 0x0C38:
    case 0x0C39:
    case 0x0C51:
      break;
    default:
      return false;
  }
  BT_HDR* command = HCI_GET_CMD_BUF(0);
  if (command == NULL) return false;
  hci_cmd_metadata_t* metadata = HCI_GET_CMD_METAMSG(command);
  metadata->opcode = opcode;
  metadata->command_complete_cb = completed;
  metadata->command_status_cb = status_received;
  metadata->context = (void*)(uintptr_t)opcode;
  command->data[0] = opcode & 0xFF;
  command->data[1] = opcode >> 8;
  command->data[2] = 0;
  hci_layer_get_interface()->transmit_command(command, completed, status_received, metadata->context);
  return true;
}
