#include "overlay.h"
#include "traffic.h"
#include "voxel.h"

#define OV_RADAR_END    (OV_TRACKS + TR_MAX * sizeof(struct track))
#define OV_COCKPIT_END  (OV_COCKPIT + sizeof(struct vox_ram))
#define OVERLAY_SIZE    (OV_RADAR_END > OV_COCKPIT_END ? OV_RADAR_END : OV_COCKPIT_END)

_Static_assert(OV_TRACKS % 4 == 0, "track table must stay word aligned");
_Static_assert(OV_COCKPIT_END <= OV_RADAR_END, "cockpit mode must fit the radar's share, or static RAM grows");

u8 overlay_ram[OVERLAY_SIZE] __attribute__((aligned(4)));
