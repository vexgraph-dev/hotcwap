#ifndef HOTCWAP_TYPE_H
#define HOTCWAP_TYPE_H

#include <stdint.h>

// hotcwap-type.h — the hotcwap project's ABI constants.
//
// OWNERSHIP: hotcwap implements no registry classes (threads live in
// vexspoke; windows carry no numeric IDs), so this file holds the
// module ABI surface instead: the Vulkan module's manifest type values
// and the behavior-state contract value. Included by the .c files that
// publish them — never by vexspoke (Rule 17: upstream builds standalone).

// --- Vulkan module manifest (hot/vk_module.c s_manifest) ---
#define ID_VK_SWAPCHAIN   0x00003Eu
#define ID_VK_PIPELINE    0x000044u
#define ID_VK_RENDER_PASS 0x000041u
#define ID_VK_FRAMEBUFFER 0x000049u

#endif
