/* Addresses and structure offsets in Thief2MP.exe, NewDark 1.29 MP beta.
 * SHA-256 e86b0645295e24f85747607c106beb9e94e4c21314f09156d4813c931d521fe0,
 * PE timestamp 0x6a976831, SizeOfImage 0x99b000, preferred base 0x400000.
 * All addresses are preferred VAs; use VA() to rebase (the exe is ASLR-enabled).
 * Names are ours, derived from the decompile (see games/thief-2/NOTES.md). */
#pragma once
#include <stdint.h>

#define EXE_TIMESTAMP   0x6a976831u
#define EXE_IMAGESIZE   0x99b000u
#define EXE_PREF_BASE   0x400000u

extern uintptr_t g_base;
#define VA(a) ((void *)(g_base + ((uintptr_t)(a) - EXE_PREF_BASE)))

/* Functions (all cdecl unless noted) */
#define A_GHOST_FRAME        0x5516e0  /* void (int dt_ms): per-frame ghost tick, locals then remotes */
#define A_LOCAL_TICK         0x5556a0  /* void (LocalGhost*, float dt_s) */
#define A_SEND_DECIDE        0x554fe0  /* int (LocalGhost*, Heartbeat* out): fills *out, 1 = send now */
#define A_SEND               0x554d40  /* void (LocalGhost*, Heartbeat*) */
#define A_REMOTE_PREDICT     0x553bd0  /* void (RemoteGhost*, float dt_s): dead-reckon pred from info */
#define A_REMOTE_STEER       0x553dc0  /* void (RemoteGhost*, float dt_s): move avatar toward pred */
#define A_RECV_HEARTBEAT     0x5539d0  /* void (int obj, int seq, int relobj, Heartbeat*, short*) */
#define A_PHYS_GET_MODEL     0x516c90  /* cPhysModel* __stdcall (int obj) */
#define A_COMMAND_EXECUTE    0x41a940  /* char* (EAX = char* cmd): engine command line */
#define A_OBJ_POS_UPDATE     0x559c30  /* void (ECX = float pos[3], EDX = obj, [stack] = short facing[3]) */
#define A_STEER_VEL_CHECK    0x553f07  /* inside A_REMOTE_STEER: moving player, "|v|^2 <= 1 ? follow pred vel : d + pred vel"
                                          where v is three stack floats never written on this path (engine bug) */
#define A_NET_DAMAGE_FILTER  0x44bce0  /* int __stdcall (int victim, int culprit, sDamageMsg*, void*): MP damage
                                          filter; returns 1 after zeroing the damage to veto it, 0 = no opinion */

/* Globals */
#define A_SIM_TIME_MS        0x9b3d50  /* uint32 sim time, ms */
#define A_INPUT_BINDER       0xab8280  /* IInputBinder*: vtable +0x30 = ProcessCmd(this, char*) */
#define A_APP_AGGREGATE      0xa7ea4c  /* IUnknown* of the app aggregate: QueryInterface for components */
#define A_IID_NETMANAGER     0x80963c  /* IID the UI uses to get the net manager */
#define A_NETMANAGER_VTABLE  0x7d5a3c  /* cNetManager vtable; slot 40 = SetMaxPlayers(this, n) __stdcall */
#define NETMAN_SLOT_SETMAX   40
#define NETMAN_MAXPLAYERS    0x136     /* field: join accepted while players < this */
#define A_OBJ_NETWORKING     0xab83ac  /* IObjectNetworking*: vtable slots below, each BOOL __stdcall (this, obj) */
#define OBJNET_IS_PROXY      0x1c      /* owned by another machine */
#define OBJNET_HOSTED_HERE   0x20      /* owned by this machine */
#define OBJNET_LOCAL_ONLY    0x24      /* NetworkCategory Local Only: every machine has its own copy */
#define A_REMOTE_GHOSTS      0x8c5a80  /* hash of RemoteGhost*: +4 bucket array, +8 bucket count; node {RemoteGhost*, next} */
#define A_POSITION_TABLE     0xab9e60  /* -> {.., +0x18 Position*[obj], +0x20 count}; Position: vec +0, facing +0x10 */

/* Signatures: first bytes at each hooked function, checked before hooking. */
#define SIG_RECV_HEARTBEAT   "\x8b\x44\x24\x04\x83\xec\x18\x53\x8b\x5c\x24\x24\x56\x57\x50"
#define SIG_GHOST_FRAME      "\x55\x8b\xec\x83\xe4\xf8\x83\xec\x14\x56\x33\xf6"
#define SIG_SEND_DECIDE      "\x55\x8b\xec\x83\xe4\xf8\x83\xec\x2c\x53\x8b\x5d\x0c"
#define SIG_SEND             "\x83\xec\x0c\x53\x8b\x5c\x24\x14\x66\xff\x43\x64"
#define SIG_REMOTE_STEER     "\x83\xec\x24\x53\x55\x8b\x6c\x24\x30\x56\x8b\x75\x00"
#define SIG_PHYS_GET_MODEL   "\x83\xec\x14\x53\x8b\x5c\x24\x1c\x56\x57\x53"
#define SIG_NET_DAMAGE_FILTER "\x53\x55\x8b\x6c\x24\x14\x8b\x5d\x1c\x8b\xc5"
/* A_STEER_VEL_CHECK .. the jp to the "follow pred vel" branch (0x553fbd): fld [esp+0x14]; mov edx,1;
 * fld [esp+0x10]; fld [esp+0x18]; |.|^2; fcomp 1.0; jp. [esp+0x1c..0x24] holds d = pred - physics pos. */
#define SIG_STEER_VEL_CHECK  "\xd9\x44\x24\x14\xba\x01\x00\x00\x00\xd9\x44\x24\x10\xd9\x44\x24\x18\xd9\xc1\xde\xca" \
                             "\xd9\xc2\xde\xcb\xd9\xc9\xde\xc2\xdc\xc8\xde\xc1\xd9\x5c\x24\x38\xd8\x5c\x24\x38\xdf\xe0" \
                             "\xf6\xc4\x05\x0f\x8a\x82\x00\x00\x00"
#define SIG_OBJ_POS_UPDATE   "\xa1\x60\x9e\xab\x00\x56\x8b\x74\x24\x08\x3b\x50\x20"   /* absolute: compare rebased */

/* cPhysModel */
#define PM_POS               0x34      /* float[3] world position of the model */

/* Heartbeat / ghost state block (0x22 bytes, packed) */
#define HB_POS               0x00      /* float[3] */
#define HB_VEL               0x0c      /* float[3] */
#define HB_ANGVEL_Z          0x1a      /* short, heading rate */
#define HB_HEADING           0x1c      /* ushort heading (0..65535) */
#define HB_FLAGS             0x1e      /* ushort */
#define HB_MODE              0x20      /* uchar */
#define HB_SIZE              0x22

/* Local ghost (one per object this machine owns and sends) */
#define LG_OBJ               0x00
#define LG_FLAGS             0x0c      /* (flags & 3) == 3: player */
#define LG_SENT              0x10      /* Heartbeat block: last state sent */
#define LG_LAST_SEND_MS      0x36      /* sim time of last send */
#define LG_SEQ               0x64      /* ushort, incremented per send */

/* Remote ghost (one per object owned by another machine) */
#define RG_OBJ               0x00
#define RG_FLAGS             0x08      /* (flags & 3) == 3: player; 0x200000: force warp this frame */
#define RG_INFO              0x0c      /* Heartbeat block: last received */
#define RG_RECV_MS           0x32      /* sim time last heartbeat applied */
#define RG_PRED              0x36      /* Heartbeat block: dead-reckoned prediction */
#define RG_RELOBJ            0x58      /* int: positions relative to this object (0 = world) */
#define RG_SEQ               0x60      /* ushort */

#define GF_TYPE_MASK         3
#define GF_TYPE_PLAYER       3
#define RGF_FORCE_WARP       0x200000u

#define FLD(p, off, T)       (*(T *)((uint8_t *)(p) + (off)))
