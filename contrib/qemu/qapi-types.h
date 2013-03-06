/* AUTOMATICALLY GENERATED, DO NOT MODIFY */

/*
 * schema-defined QAPI types
 *
 * Copyright IBM, Corp. 2011
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 */

#ifndef QAPI_TYPES_H
#define QAPI_TYPES_H

#include <stdbool.h>
#include <stdint.h>


#ifndef QAPI_TYPES_BUILTIN_STRUCT_DECL_H
#define QAPI_TYPES_BUILTIN_STRUCT_DECL_H


typedef struct strList
{
    union {
        char * value;
        uint64_t padding;
    };
    struct strList *next;
} strList;

typedef struct intList
{
    union {
        int64_t value;
        uint64_t padding;
    };
    struct intList *next;
} intList;

typedef struct numberList
{
    union {
        double value;
        uint64_t padding;
    };
    struct numberList *next;
} numberList;

typedef struct boolList
{
    union {
        bool value;
        uint64_t padding;
    };
    struct boolList *next;
} boolList;

typedef struct int8List
{
    union {
        int8_t value;
        uint64_t padding;
    };
    struct int8List *next;
} int8List;

typedef struct int16List
{
    union {
        int16_t value;
        uint64_t padding;
    };
    struct int16List *next;
} int16List;

typedef struct int32List
{
    union {
        int32_t value;
        uint64_t padding;
    };
    struct int32List *next;
} int32List;

typedef struct int64List
{
    union {
        int64_t value;
        uint64_t padding;
    };
    struct int64List *next;
} int64List;

typedef struct uint8List
{
    union {
        uint8_t value;
        uint64_t padding;
    };
    struct uint8List *next;
} uint8List;

typedef struct uint16List
{
    union {
        uint16_t value;
        uint64_t padding;
    };
    struct uint16List *next;
} uint16List;

typedef struct uint32List
{
    union {
        uint32_t value;
        uint64_t padding;
    };
    struct uint32List *next;
} uint32List;

typedef struct uint64List
{
    union {
        uint64_t value;
        uint64_t padding;
    };
    struct uint64List *next;
} uint64List;

#endif /* QAPI_TYPES_BUILTIN_STRUCT_DECL_H */


extern const char *ErrorClass_lookup[];
typedef enum ErrorClass
{
    ERROR_CLASS_GENERIC_ERROR = 0,
    ERROR_CLASS_COMMAND_NOT_FOUND = 1,
    ERROR_CLASS_DEVICE_ENCRYPTED = 2,
    ERROR_CLASS_DEVICE_NOT_ACTIVE = 3,
    ERROR_CLASS_DEVICE_NOT_FOUND = 4,
    ERROR_CLASS_K_V_M_MISSING_CAP = 5,
    ERROR_CLASS_MAX = 6,
} ErrorClass;

typedef struct ErrorClassList
{
    ErrorClass value;
    struct ErrorClassList *next;
} ErrorClassList;


typedef struct NameInfo NameInfo;

typedef struct NameInfoList
{
    union {
        NameInfo *value;
        uint64_t padding;
    };
    struct NameInfoList *next;
} NameInfoList;


typedef struct VersionInfo VersionInfo;

typedef struct VersionInfoList
{
    union {
        VersionInfo *value;
        uint64_t padding;
    };
    struct VersionInfoList *next;
} VersionInfoList;


typedef struct KvmInfo KvmInfo;

typedef struct KvmInfoList
{
    union {
        KvmInfo *value;
        uint64_t padding;
    };
    struct KvmInfoList *next;
} KvmInfoList;

extern const char *RunState_lookup[];
typedef enum RunState
{
    RUN_STATE_DEBUG = 0,
    RUN_STATE_INMIGRATE = 1,
    RUN_STATE_INTERNAL_ERROR = 2,
    RUN_STATE_IO_ERROR = 3,
    RUN_STATE_PAUSED = 4,
    RUN_STATE_POSTMIGRATE = 5,
    RUN_STATE_PRELAUNCH = 6,
    RUN_STATE_FINISH_MIGRATE = 7,
    RUN_STATE_RESTORE_VM = 8,
    RUN_STATE_RUNNING = 9,
    RUN_STATE_SAVE_VM = 10,
    RUN_STATE_SHUTDOWN = 11,
    RUN_STATE_SUSPENDED = 12,
    RUN_STATE_WATCHDOG = 13,
    RUN_STATE_GUEST_PANICKED = 14,
    RUN_STATE_MAX = 15,
} RunState;

typedef struct RunStateList
{
    RunState value;
    struct RunStateList *next;
} RunStateList;


typedef struct SnapshotInfo SnapshotInfo;

typedef struct SnapshotInfoList
{
    union {
        SnapshotInfo *value;
        uint64_t padding;
    };
    struct SnapshotInfoList *next;
} SnapshotInfoList;


typedef struct ImageInfo ImageInfo;

typedef struct ImageInfoList
{
    union {
        ImageInfo *value;
        uint64_t padding;
    };
    struct ImageInfoList *next;
} ImageInfoList;


typedef struct ImageCheck ImageCheck;

typedef struct ImageCheckList
{
    union {
        ImageCheck *value;
        uint64_t padding;
    };
    struct ImageCheckList *next;
} ImageCheckList;


typedef struct StatusInfo StatusInfo;

typedef struct StatusInfoList
{
    union {
        StatusInfo *value;
        uint64_t padding;
    };
    struct StatusInfoList *next;
} StatusInfoList;


typedef struct UuidInfo UuidInfo;

typedef struct UuidInfoList
{
    union {
        UuidInfo *value;
        uint64_t padding;
    };
    struct UuidInfoList *next;
} UuidInfoList;


typedef struct ChardevInfo ChardevInfo;

typedef struct ChardevInfoList
{
    union {
        ChardevInfo *value;
        uint64_t padding;
    };
    struct ChardevInfoList *next;
} ChardevInfoList;

extern const char *DataFormat_lookup[];
typedef enum DataFormat
{
    DATA_FORMAT_UTF8 = 0,
    DATA_FORMAT_BASE64 = 1,
    DATA_FORMAT_MAX = 2,
} DataFormat;

typedef struct DataFormatList
{
    DataFormat value;
    struct DataFormatList *next;
} DataFormatList;


typedef struct CommandInfo CommandInfo;

typedef struct CommandInfoList
{
    union {
        CommandInfo *value;
        uint64_t padding;
    };
    struct CommandInfoList *next;
} CommandInfoList;


typedef struct EventInfo EventInfo;

typedef struct EventInfoList
{
    union {
        EventInfo *value;
        uint64_t padding;
    };
    struct EventInfoList *next;
} EventInfoList;


typedef struct MigrationStats MigrationStats;

typedef struct MigrationStatsList
{
    union {
        MigrationStats *value;
        uint64_t padding;
    };
    struct MigrationStatsList *next;
} MigrationStatsList;


typedef struct XBZRLECacheStats XBZRLECacheStats;

typedef struct XBZRLECacheStatsList
{
    union {
        XBZRLECacheStats *value;
        uint64_t padding;
    };
    struct XBZRLECacheStatsList *next;
} XBZRLECacheStatsList;


typedef struct MigrationInfo MigrationInfo;

typedef struct MigrationInfoList
{
    union {
        MigrationInfo *value;
        uint64_t padding;
    };
    struct MigrationInfoList *next;
} MigrationInfoList;

extern const char *MigrationCapability_lookup[];
typedef enum MigrationCapability
{
    MIGRATION_CAPABILITY_XBZRLE = 0,
    MIGRATION_CAPABILITY_X_RDMA_PIN_ALL = 1,
    MIGRATION_CAPABILITY_AUTO_CONVERGE = 2,
    MIGRATION_CAPABILITY_MAX = 3,
} MigrationCapability;

typedef struct MigrationCapabilityList
{
    MigrationCapability value;
    struct MigrationCapabilityList *next;
} MigrationCapabilityList;


typedef struct MigrationCapabilityStatus MigrationCapabilityStatus;

typedef struct MigrationCapabilityStatusList
{
    union {
        MigrationCapabilityStatus *value;
        uint64_t padding;
    };
    struct MigrationCapabilityStatusList *next;
} MigrationCapabilityStatusList;


typedef struct MouseInfo MouseInfo;

typedef struct MouseInfoList
{
    union {
        MouseInfo *value;
        uint64_t padding;
    };
    struct MouseInfoList *next;
} MouseInfoList;


typedef struct CpuInfo CpuInfo;

typedef struct CpuInfoList
{
    union {
        CpuInfo *value;
        uint64_t padding;
    };
    struct CpuInfoList *next;
} CpuInfoList;


typedef struct BlockDeviceInfo BlockDeviceInfo;

typedef struct BlockDeviceInfoList
{
    union {
        BlockDeviceInfo *value;
        uint64_t padding;
    };
    struct BlockDeviceInfoList *next;
} BlockDeviceInfoList;

extern const char *BlockDeviceIoStatus_lookup[];
typedef enum BlockDeviceIoStatus
{
    BLOCK_DEVICE_IO_STATUS_OK = 0,
    BLOCK_DEVICE_IO_STATUS_FAILED = 1,
    BLOCK_DEVICE_IO_STATUS_NOSPACE = 2,
    BLOCK_DEVICE_IO_STATUS_MAX = 3,
} BlockDeviceIoStatus;

typedef struct BlockDeviceIoStatusList
{
    BlockDeviceIoStatus value;
    struct BlockDeviceIoStatusList *next;
} BlockDeviceIoStatusList;


typedef struct BlockDirtyInfo BlockDirtyInfo;

typedef struct BlockDirtyInfoList
{
    union {
        BlockDirtyInfo *value;
        uint64_t padding;
    };
    struct BlockDirtyInfoList *next;
} BlockDirtyInfoList;


typedef struct BlockInfo BlockInfo;

typedef struct BlockInfoList
{
    union {
        BlockInfo *value;
        uint64_t padding;
    };
    struct BlockInfoList *next;
} BlockInfoList;


typedef struct BlockDeviceStats BlockDeviceStats;

typedef struct BlockDeviceStatsList
{
    union {
        BlockDeviceStats *value;
        uint64_t padding;
    };
    struct BlockDeviceStatsList *next;
} BlockDeviceStatsList;


typedef struct BlockStats BlockStats;

typedef struct BlockStatsList
{
    union {
        BlockStats *value;
        uint64_t padding;
    };
    struct BlockStatsList *next;
} BlockStatsList;


typedef struct VncClientInfo VncClientInfo;

typedef struct VncClientInfoList
{
    union {
        VncClientInfo *value;
        uint64_t padding;
    };
    struct VncClientInfoList *next;
} VncClientInfoList;


typedef struct VncInfo VncInfo;

typedef struct VncInfoList
{
    union {
        VncInfo *value;
        uint64_t padding;
    };
    struct VncInfoList *next;
} VncInfoList;


typedef struct SpiceChannel SpiceChannel;

typedef struct SpiceChannelList
{
    union {
        SpiceChannel *value;
        uint64_t padding;
    };
    struct SpiceChannelList *next;
} SpiceChannelList;

extern const char *SpiceQueryMouseMode_lookup[];
typedef enum SpiceQueryMouseMode
{
    SPICE_QUERY_MOUSE_MODE_CLIENT = 0,
    SPICE_QUERY_MOUSE_MODE_SERVER = 1,
    SPICE_QUERY_MOUSE_MODE_UNKNOWN = 2,
    SPICE_QUERY_MOUSE_MODE_MAX = 3,
} SpiceQueryMouseMode;

typedef struct SpiceQueryMouseModeList
{
    SpiceQueryMouseMode value;
    struct SpiceQueryMouseModeList *next;
} SpiceQueryMouseModeList;


typedef struct SpiceInfo SpiceInfo;

typedef struct SpiceInfoList
{
    union {
        SpiceInfo *value;
        uint64_t padding;
    };
    struct SpiceInfoList *next;
} SpiceInfoList;


typedef struct BalloonInfo BalloonInfo;

typedef struct BalloonInfoList
{
    union {
        BalloonInfo *value;
        uint64_t padding;
    };
    struct BalloonInfoList *next;
} BalloonInfoList;


typedef struct PciMemoryRange PciMemoryRange;

typedef struct PciMemoryRangeList
{
    union {
        PciMemoryRange *value;
        uint64_t padding;
    };
    struct PciMemoryRangeList *next;
} PciMemoryRangeList;


typedef struct PciMemoryRegion PciMemoryRegion;

typedef struct PciMemoryRegionList
{
    union {
        PciMemoryRegion *value;
        uint64_t padding;
    };
    struct PciMemoryRegionList *next;
} PciMemoryRegionList;


typedef struct PciBridgeInfo PciBridgeInfo;

typedef struct PciBridgeInfoList
{
    union {
        PciBridgeInfo *value;
        uint64_t padding;
    };
    struct PciBridgeInfoList *next;
} PciBridgeInfoList;


typedef struct PciDeviceInfo PciDeviceInfo;

typedef struct PciDeviceInfoList
{
    union {
        PciDeviceInfo *value;
        uint64_t padding;
    };
    struct PciDeviceInfoList *next;
} PciDeviceInfoList;


typedef struct PciInfo PciInfo;

typedef struct PciInfoList
{
    union {
        PciInfo *value;
        uint64_t padding;
    };
    struct PciInfoList *next;
} PciInfoList;

extern const char *BlockdevOnError_lookup[];
typedef enum BlockdevOnError
{
    BLOCKDEV_ON_ERROR_REPORT = 0,
    BLOCKDEV_ON_ERROR_IGNORE = 1,
    BLOCKDEV_ON_ERROR_ENOSPC = 2,
    BLOCKDEV_ON_ERROR_STOP = 3,
    BLOCKDEV_ON_ERROR_MAX = 4,
} BlockdevOnError;

typedef struct BlockdevOnErrorList
{
    BlockdevOnError value;
    struct BlockdevOnErrorList *next;
} BlockdevOnErrorList;

extern const char *MirrorSyncMode_lookup[];
typedef enum MirrorSyncMode
{
    MIRROR_SYNC_MODE_TOP = 0,
    MIRROR_SYNC_MODE_FULL = 1,
    MIRROR_SYNC_MODE_NONE = 2,
    MIRROR_SYNC_MODE_MAX = 3,
} MirrorSyncMode;

typedef struct MirrorSyncModeList
{
    MirrorSyncMode value;
    struct MirrorSyncModeList *next;
} MirrorSyncModeList;


typedef struct BlockJobInfo BlockJobInfo;

typedef struct BlockJobInfoList
{
    union {
        BlockJobInfo *value;
        uint64_t padding;
    };
    struct BlockJobInfoList *next;
} BlockJobInfoList;

extern const char *NewImageMode_lookup[];
typedef enum NewImageMode
{
    NEW_IMAGE_MODE_EXISTING = 0,
    NEW_IMAGE_MODE_ABSOLUTE_PATHS = 1,
    NEW_IMAGE_MODE_MAX = 2,
} NewImageMode;

typedef struct NewImageModeList
{
    NewImageMode value;
    struct NewImageModeList *next;
} NewImageModeList;


typedef struct BlockdevSnapshot BlockdevSnapshot;

typedef struct BlockdevSnapshotList
{
    union {
        BlockdevSnapshot *value;
        uint64_t padding;
    };
    struct BlockdevSnapshotList *next;
} BlockdevSnapshotList;


typedef struct DriveBackup DriveBackup;

typedef struct DriveBackupList
{
    union {
        DriveBackup *value;
        uint64_t padding;
    };
    struct DriveBackupList *next;
} DriveBackupList;


typedef struct Abort Abort;

typedef struct AbortList
{
    union {
        Abort *value;
        uint64_t padding;
    };
    struct AbortList *next;
} AbortList;


typedef struct TransactionAction TransactionAction;

typedef struct TransactionActionList
{
    union {
        TransactionAction *value;
        uint64_t padding;
    };
    struct TransactionActionList *next;
} TransactionActionList;

extern const char *TransactionActionKind_lookup[];
typedef enum TransactionActionKind
{
    TRANSACTION_ACTION_KIND_BLOCKDEV_SNAPSHOT_SYNC = 0,
    TRANSACTION_ACTION_KIND_DRIVE_BACKUP = 1,
    TRANSACTION_ACTION_KIND_ABORT = 2,
    TRANSACTION_ACTION_KIND_MAX = 3,
} TransactionActionKind;


typedef struct ObjectPropertyInfo ObjectPropertyInfo;

typedef struct ObjectPropertyInfoList
{
    union {
        ObjectPropertyInfo *value;
        uint64_t padding;
    };
    struct ObjectPropertyInfoList *next;
} ObjectPropertyInfoList;


typedef struct ObjectTypeInfo ObjectTypeInfo;

typedef struct ObjectTypeInfoList
{
    union {
        ObjectTypeInfo *value;
        uint64_t padding;
    };
    struct ObjectTypeInfoList *next;
} ObjectTypeInfoList;


typedef struct DevicePropertyInfo DevicePropertyInfo;

typedef struct DevicePropertyInfoList
{
    union {
        DevicePropertyInfo *value;
        uint64_t padding;
    };
    struct DevicePropertyInfoList *next;
} DevicePropertyInfoList;


typedef struct NetdevNoneOptions NetdevNoneOptions;

typedef struct NetdevNoneOptionsList
{
    union {
        NetdevNoneOptions *value;
        uint64_t padding;
    };
    struct NetdevNoneOptionsList *next;
} NetdevNoneOptionsList;


typedef struct NetLegacyNicOptions NetLegacyNicOptions;

typedef struct NetLegacyNicOptionsList
{
    union {
        NetLegacyNicOptions *value;
        uint64_t padding;
    };
    struct NetLegacyNicOptionsList *next;
} NetLegacyNicOptionsList;


typedef struct String String;

typedef struct StringList
{
    union {
        String *value;
        uint64_t padding;
    };
    struct StringList *next;
} StringList;


typedef struct NetdevUserOptions NetdevUserOptions;

typedef struct NetdevUserOptionsList
{
    union {
        NetdevUserOptions *value;
        uint64_t padding;
    };
    struct NetdevUserOptionsList *next;
} NetdevUserOptionsList;


typedef struct NetdevTapOptions NetdevTapOptions;

typedef struct NetdevTapOptionsList
{
    union {
        NetdevTapOptions *value;
        uint64_t padding;
    };
    struct NetdevTapOptionsList *next;
} NetdevTapOptionsList;


typedef struct NetdevSocketOptions NetdevSocketOptions;

typedef struct NetdevSocketOptionsList
{
    union {
        NetdevSocketOptions *value;
        uint64_t padding;
    };
    struct NetdevSocketOptionsList *next;
} NetdevSocketOptionsList;


typedef struct NetdevVdeOptions NetdevVdeOptions;

typedef struct NetdevVdeOptionsList
{
    union {
        NetdevVdeOptions *value;
        uint64_t padding;
    };
    struct NetdevVdeOptionsList *next;
} NetdevVdeOptionsList;


typedef struct NetdevDumpOptions NetdevDumpOptions;

typedef struct NetdevDumpOptionsList
{
    union {
        NetdevDumpOptions *value;
        uint64_t padding;
    };
    struct NetdevDumpOptionsList *next;
} NetdevDumpOptionsList;


typedef struct NetdevBridgeOptions NetdevBridgeOptions;

typedef struct NetdevBridgeOptionsList
{
    union {
        NetdevBridgeOptions *value;
        uint64_t padding;
    };
    struct NetdevBridgeOptionsList *next;
} NetdevBridgeOptionsList;


typedef struct NetdevHubPortOptions NetdevHubPortOptions;

typedef struct NetdevHubPortOptionsList
{
    union {
        NetdevHubPortOptions *value;
        uint64_t padding;
    };
    struct NetdevHubPortOptionsList *next;
} NetdevHubPortOptionsList;


typedef struct NetClientOptions NetClientOptions;

typedef struct NetClientOptionsList
{
    union {
        NetClientOptions *value;
        uint64_t padding;
    };
    struct NetClientOptionsList *next;
} NetClientOptionsList;

extern const char *NetClientOptionsKind_lookup[];
typedef enum NetClientOptionsKind
{
    NET_CLIENT_OPTIONS_KIND_NONE = 0,
    NET_CLIENT_OPTIONS_KIND_NIC = 1,
    NET_CLIENT_OPTIONS_KIND_USER = 2,
    NET_CLIENT_OPTIONS_KIND_TAP = 3,
    NET_CLIENT_OPTIONS_KIND_SOCKET = 4,
    NET_CLIENT_OPTIONS_KIND_VDE = 5,
    NET_CLIENT_OPTIONS_KIND_DUMP = 6,
    NET_CLIENT_OPTIONS_KIND_BRIDGE = 7,
    NET_CLIENT_OPTIONS_KIND_HUBPORT = 8,
    NET_CLIENT_OPTIONS_KIND_MAX = 9,
} NetClientOptionsKind;


typedef struct NetLegacy NetLegacy;

typedef struct NetLegacyList
{
    union {
        NetLegacy *value;
        uint64_t padding;
    };
    struct NetLegacyList *next;
} NetLegacyList;


typedef struct Netdev Netdev;

typedef struct NetdevList
{
    union {
        Netdev *value;
        uint64_t padding;
    };
    struct NetdevList *next;
} NetdevList;


typedef struct InetSocketAddress InetSocketAddress;

typedef struct InetSocketAddressList
{
    union {
        InetSocketAddress *value;
        uint64_t padding;
    };
    struct InetSocketAddressList *next;
} InetSocketAddressList;


typedef struct UnixSocketAddress UnixSocketAddress;

typedef struct UnixSocketAddressList
{
    union {
        UnixSocketAddress *value;
        uint64_t padding;
    };
    struct UnixSocketAddressList *next;
} UnixSocketAddressList;


typedef struct SocketAddress SocketAddress;

typedef struct SocketAddressList
{
    union {
        SocketAddress *value;
        uint64_t padding;
    };
    struct SocketAddressList *next;
} SocketAddressList;

extern const char *SocketAddressKind_lookup[];
typedef enum SocketAddressKind
{
    SOCKET_ADDRESS_KIND_INET = 0,
    SOCKET_ADDRESS_KIND_UNIX = 1,
    SOCKET_ADDRESS_KIND_FD = 2,
    SOCKET_ADDRESS_KIND_MAX = 3,
} SocketAddressKind;


typedef struct MachineInfo MachineInfo;

typedef struct MachineInfoList
{
    union {
        MachineInfo *value;
        uint64_t padding;
    };
    struct MachineInfoList *next;
} MachineInfoList;


typedef struct CpuDefinitionInfo CpuDefinitionInfo;

typedef struct CpuDefinitionInfoList
{
    union {
        CpuDefinitionInfo *value;
        uint64_t padding;
    };
    struct CpuDefinitionInfoList *next;
} CpuDefinitionInfoList;


typedef struct AddfdInfo AddfdInfo;

typedef struct AddfdInfoList
{
    union {
        AddfdInfo *value;
        uint64_t padding;
    };
    struct AddfdInfoList *next;
} AddfdInfoList;


typedef struct FdsetFdInfo FdsetFdInfo;

typedef struct FdsetFdInfoList
{
    union {
        FdsetFdInfo *value;
        uint64_t padding;
    };
    struct FdsetFdInfoList *next;
} FdsetFdInfoList;


typedef struct FdsetInfo FdsetInfo;

typedef struct FdsetInfoList
{
    union {
        FdsetInfo *value;
        uint64_t padding;
    };
    struct FdsetInfoList *next;
} FdsetInfoList;


typedef struct TargetInfo TargetInfo;

typedef struct TargetInfoList
{
    union {
        TargetInfo *value;
        uint64_t padding;
    };
    struct TargetInfoList *next;
} TargetInfoList;

extern const char *QKeyCode_lookup[];
typedef enum QKeyCode
{
    Q_KEY_CODE_SHIFT = 0,
    Q_KEY_CODE_SHIFT_R = 1,
    Q_KEY_CODE_ALT = 2,
    Q_KEY_CODE_ALT_R = 3,
    Q_KEY_CODE_ALTGR = 4,
    Q_KEY_CODE_ALTGR_R = 5,
    Q_KEY_CODE_CTRL = 6,
    Q_KEY_CODE_CTRL_R = 7,
    Q_KEY_CODE_MENU = 8,
    Q_KEY_CODE_ESC = 9,
    Q_KEY_CODE_1 = 10,
    Q_KEY_CODE_2 = 11,
    Q_KEY_CODE_3 = 12,
    Q_KEY_CODE_4 = 13,
    Q_KEY_CODE_5 = 14,
    Q_KEY_CODE_6 = 15,
    Q_KEY_CODE_7 = 16,
    Q_KEY_CODE_8 = 17,
    Q_KEY_CODE_9 = 18,
    Q_KEY_CODE_0 = 19,
    Q_KEY_CODE_MINUS = 20,
    Q_KEY_CODE_EQUAL = 21,
    Q_KEY_CODE_BACKSPACE = 22,
    Q_KEY_CODE_TAB = 23,
    Q_KEY_CODE_Q = 24,
    Q_KEY_CODE_W = 25,
    Q_KEY_CODE_E = 26,
    Q_KEY_CODE_R = 27,
    Q_KEY_CODE_T = 28,
    Q_KEY_CODE_Y = 29,
    Q_KEY_CODE_U = 30,
    Q_KEY_CODE_I = 31,
    Q_KEY_CODE_O = 32,
    Q_KEY_CODE_P = 33,
    Q_KEY_CODE_BRACKET_LEFT = 34,
    Q_KEY_CODE_BRACKET_RIGHT = 35,
    Q_KEY_CODE_RET = 36,
    Q_KEY_CODE_A = 37,
    Q_KEY_CODE_S = 38,
    Q_KEY_CODE_D = 39,
    Q_KEY_CODE_F = 40,
    Q_KEY_CODE_G = 41,
    Q_KEY_CODE_H = 42,
    Q_KEY_CODE_J = 43,
    Q_KEY_CODE_K = 44,
    Q_KEY_CODE_L = 45,
    Q_KEY_CODE_SEMICOLON = 46,
    Q_KEY_CODE_APOSTROPHE = 47,
    Q_KEY_CODE_GRAVE_ACCENT = 48,
    Q_KEY_CODE_BACKSLASH = 49,
    Q_KEY_CODE_Z = 50,
    Q_KEY_CODE_X = 51,
    Q_KEY_CODE_C = 52,
    Q_KEY_CODE_V = 53,
    Q_KEY_CODE_B = 54,
    Q_KEY_CODE_N = 55,
    Q_KEY_CODE_M = 56,
    Q_KEY_CODE_COMMA = 57,
    Q_KEY_CODE_DOT = 58,
    Q_KEY_CODE_SLASH = 59,
    Q_KEY_CODE_ASTERISK = 60,
    Q_KEY_CODE_SPC = 61,
    Q_KEY_CODE_CAPS_LOCK = 62,
    Q_KEY_CODE_F1 = 63,
    Q_KEY_CODE_F2 = 64,
    Q_KEY_CODE_F3 = 65,
    Q_KEY_CODE_F4 = 66,
    Q_KEY_CODE_F5 = 67,
    Q_KEY_CODE_F6 = 68,
    Q_KEY_CODE_F7 = 69,
    Q_KEY_CODE_F8 = 70,
    Q_KEY_CODE_F9 = 71,
    Q_KEY_CODE_F10 = 72,
    Q_KEY_CODE_NUM_LOCK = 73,
    Q_KEY_CODE_SCROLL_LOCK = 74,
    Q_KEY_CODE_KP_DIVIDE = 75,
    Q_KEY_CODE_KP_MULTIPLY = 76,
    Q_KEY_CODE_KP_SUBTRACT = 77,
    Q_KEY_CODE_KP_ADD = 78,
    Q_KEY_CODE_KP_ENTER = 79,
    Q_KEY_CODE_KP_DECIMAL = 80,
    Q_KEY_CODE_SYSRQ = 81,
    Q_KEY_CODE_KP_0 = 82,
    Q_KEY_CODE_KP_1 = 83,
    Q_KEY_CODE_KP_2 = 84,
    Q_KEY_CODE_KP_3 = 85,
    Q_KEY_CODE_KP_4 = 86,
    Q_KEY_CODE_KP_5 = 87,
    Q_KEY_CODE_KP_6 = 88,
    Q_KEY_CODE_KP_7 = 89,
    Q_KEY_CODE_KP_8 = 90,
    Q_KEY_CODE_KP_9 = 91,
    Q_KEY_CODE_LESS = 92,
    Q_KEY_CODE_F11 = 93,
    Q_KEY_CODE_F12 = 94,
    Q_KEY_CODE_PRINT = 95,
    Q_KEY_CODE_HOME = 96,
    Q_KEY_CODE_PGUP = 97,
    Q_KEY_CODE_PGDN = 98,
    Q_KEY_CODE_END = 99,
    Q_KEY_CODE_LEFT = 100,
    Q_KEY_CODE_UP = 101,
    Q_KEY_CODE_DOWN = 102,
    Q_KEY_CODE_RIGHT = 103,
    Q_KEY_CODE_INSERT = 104,
    Q_KEY_CODE_DELETE = 105,
    Q_KEY_CODE_STOP = 106,
    Q_KEY_CODE_AGAIN = 107,
    Q_KEY_CODE_PROPS = 108,
    Q_KEY_CODE_UNDO = 109,
    Q_KEY_CODE_FRONT = 110,
    Q_KEY_CODE_COPY = 111,
    Q_KEY_CODE_OPEN = 112,
    Q_KEY_CODE_PASTE = 113,
    Q_KEY_CODE_FIND = 114,
    Q_KEY_CODE_CUT = 115,
    Q_KEY_CODE_LF = 116,
    Q_KEY_CODE_HELP = 117,
    Q_KEY_CODE_META_L = 118,
    Q_KEY_CODE_META_R = 119,
    Q_KEY_CODE_COMPOSE = 120,
    Q_KEY_CODE_MAX = 121,
} QKeyCode;

typedef struct QKeyCodeList
{
    QKeyCode value;
    struct QKeyCodeList *next;
} QKeyCodeList;


typedef struct KeyValue KeyValue;

typedef struct KeyValueList
{
    union {
        KeyValue *value;
        uint64_t padding;
    };
    struct KeyValueList *next;
} KeyValueList;

extern const char *KeyValueKind_lookup[];
typedef enum KeyValueKind
{
    KEY_VALUE_KIND_NUMBER = 0,
    KEY_VALUE_KIND_QCODE = 1,
    KEY_VALUE_KIND_MAX = 2,
} KeyValueKind;


typedef struct ChardevFile ChardevFile;

typedef struct ChardevFileList
{
    union {
        ChardevFile *value;
        uint64_t padding;
    };
    struct ChardevFileList *next;
} ChardevFileList;


typedef struct ChardevHostdev ChardevHostdev;

typedef struct ChardevHostdevList
{
    union {
        ChardevHostdev *value;
        uint64_t padding;
    };
    struct ChardevHostdevList *next;
} ChardevHostdevList;


typedef struct ChardevSocket ChardevSocket;

typedef struct ChardevSocketList
{
    union {
        ChardevSocket *value;
        uint64_t padding;
    };
    struct ChardevSocketList *next;
} ChardevSocketList;


typedef struct ChardevUdp ChardevUdp;

typedef struct ChardevUdpList
{
    union {
        ChardevUdp *value;
        uint64_t padding;
    };
    struct ChardevUdpList *next;
} ChardevUdpList;


typedef struct ChardevMux ChardevMux;

typedef struct ChardevMuxList
{
    union {
        ChardevMux *value;
        uint64_t padding;
    };
    struct ChardevMuxList *next;
} ChardevMuxList;


typedef struct ChardevStdio ChardevStdio;

typedef struct ChardevStdioList
{
    union {
        ChardevStdio *value;
        uint64_t padding;
    };
    struct ChardevStdioList *next;
} ChardevStdioList;


typedef struct ChardevSpiceChannel ChardevSpiceChannel;

typedef struct ChardevSpiceChannelList
{
    union {
        ChardevSpiceChannel *value;
        uint64_t padding;
    };
    struct ChardevSpiceChannelList *next;
} ChardevSpiceChannelList;


typedef struct ChardevSpicePort ChardevSpicePort;

typedef struct ChardevSpicePortList
{
    union {
        ChardevSpicePort *value;
        uint64_t padding;
    };
    struct ChardevSpicePortList *next;
} ChardevSpicePortList;


typedef struct ChardevVC ChardevVC;

typedef struct ChardevVCList
{
    union {
        ChardevVC *value;
        uint64_t padding;
    };
    struct ChardevVCList *next;
} ChardevVCList;


typedef struct ChardevMemory ChardevMemory;

typedef struct ChardevMemoryList
{
    union {
        ChardevMemory *value;
        uint64_t padding;
    };
    struct ChardevMemoryList *next;
} ChardevMemoryList;


typedef struct ChardevDummy ChardevDummy;

typedef struct ChardevDummyList
{
    union {
        ChardevDummy *value;
        uint64_t padding;
    };
    struct ChardevDummyList *next;
} ChardevDummyList;


typedef struct ChardevBackend ChardevBackend;

typedef struct ChardevBackendList
{
    union {
        ChardevBackend *value;
        uint64_t padding;
    };
    struct ChardevBackendList *next;
} ChardevBackendList;

extern const char *ChardevBackendKind_lookup[];
typedef enum ChardevBackendKind
{
    CHARDEV_BACKEND_KIND_FILE = 0,
    CHARDEV_BACKEND_KIND_SERIAL = 1,
    CHARDEV_BACKEND_KIND_PARALLEL = 2,
    CHARDEV_BACKEND_KIND_PIPE = 3,
    CHARDEV_BACKEND_KIND_SOCKET = 4,
    CHARDEV_BACKEND_KIND_UDP = 5,
    CHARDEV_BACKEND_KIND_PTY = 6,
    CHARDEV_BACKEND_KIND_NULL = 7,
    CHARDEV_BACKEND_KIND_MUX = 8,
    CHARDEV_BACKEND_KIND_MSMOUSE = 9,
    CHARDEV_BACKEND_KIND_BRAILLE = 10,
    CHARDEV_BACKEND_KIND_STDIO = 11,
    CHARDEV_BACKEND_KIND_CONSOLE = 12,
    CHARDEV_BACKEND_KIND_SPICEVMC = 13,
    CHARDEV_BACKEND_KIND_SPICEPORT = 14,
    CHARDEV_BACKEND_KIND_VC = 15,
    CHARDEV_BACKEND_KIND_MEMORY = 16,
    CHARDEV_BACKEND_KIND_MAX = 17,
} ChardevBackendKind;


typedef struct ChardevReturn ChardevReturn;

typedef struct ChardevReturnList
{
    union {
        ChardevReturn *value;
        uint64_t padding;
    };
    struct ChardevReturnList *next;
} ChardevReturnList;

extern const char *TpmModel_lookup[];
typedef enum TpmModel
{
    TPM_MODEL_TPM_TIS = 0,
    TPM_MODEL_MAX = 1,
} TpmModel;

typedef struct TpmModelList
{
    TpmModel value;
    struct TpmModelList *next;
} TpmModelList;

extern const char *TpmType_lookup[];
typedef enum TpmType
{
    TPM_TYPE_PASSTHROUGH = 0,
    TPM_TYPE_MAX = 1,
} TpmType;

typedef struct TpmTypeList
{
    TpmType value;
    struct TpmTypeList *next;
} TpmTypeList;


typedef struct TPMPassthroughOptions TPMPassthroughOptions;

typedef struct TPMPassthroughOptionsList
{
    union {
        TPMPassthroughOptions *value;
        uint64_t padding;
    };
    struct TPMPassthroughOptionsList *next;
} TPMPassthroughOptionsList;


typedef struct TpmTypeOptions TpmTypeOptions;

typedef struct TpmTypeOptionsList
{
    union {
        TpmTypeOptions *value;
        uint64_t padding;
    };
    struct TpmTypeOptionsList *next;
} TpmTypeOptionsList;

extern const char *TpmTypeOptionsKind_lookup[];
typedef enum TpmTypeOptionsKind
{
    TPM_TYPE_OPTIONS_KIND_PASSTHROUGH = 0,
    TPM_TYPE_OPTIONS_KIND_MAX = 1,
} TpmTypeOptionsKind;


typedef struct TPMInfo TPMInfo;

typedef struct TPMInfoList
{
    union {
        TPMInfo *value;
        uint64_t padding;
    };
    struct TPMInfoList *next;
} TPMInfoList;


typedef struct AcpiTableOptions AcpiTableOptions;

typedef struct AcpiTableOptionsList
{
    union {
        AcpiTableOptions *value;
        uint64_t padding;
    };
    struct AcpiTableOptionsList *next;
} AcpiTableOptionsList;

extern const char *CommandLineParameterType_lookup[];
typedef enum CommandLineParameterType
{
    COMMAND_LINE_PARAMETER_TYPE_STRING = 0,
    COMMAND_LINE_PARAMETER_TYPE_BOOLEAN = 1,
    COMMAND_LINE_PARAMETER_TYPE_NUMBER = 2,
    COMMAND_LINE_PARAMETER_TYPE_SIZE = 3,
    COMMAND_LINE_PARAMETER_TYPE_MAX = 4,
} CommandLineParameterType;

typedef struct CommandLineParameterTypeList
{
    CommandLineParameterType value;
    struct CommandLineParameterTypeList *next;
} CommandLineParameterTypeList;


typedef struct CommandLineParameterInfo CommandLineParameterInfo;

typedef struct CommandLineParameterInfoList
{
    union {
        CommandLineParameterInfo *value;
        uint64_t padding;
    };
    struct CommandLineParameterInfoList *next;
} CommandLineParameterInfoList;


typedef struct CommandLineOptionInfo CommandLineOptionInfo;

typedef struct CommandLineOptionInfoList
{
    union {
        CommandLineOptionInfo *value;
        uint64_t padding;
    };
    struct CommandLineOptionInfoList *next;
} CommandLineOptionInfoList;

extern const char *X86CPURegister32_lookup[];
typedef enum X86CPURegister32
{
    X86_C_P_U_REGISTER32_EAX = 0,
    X86_C_P_U_REGISTER32_EBX = 1,
    X86_C_P_U_REGISTER32_ECX = 2,
    X86_C_P_U_REGISTER32_EDX = 3,
    X86_C_P_U_REGISTER32_ESP = 4,
    X86_C_P_U_REGISTER32_EBP = 5,
    X86_C_P_U_REGISTER32_ESI = 6,
    X86_C_P_U_REGISTER32_EDI = 7,
    X86_C_P_U_REGISTER32_MAX = 8,
} X86CPURegister32;

typedef struct X86CPURegister32List
{
    X86CPURegister32 value;
    struct X86CPURegister32List *next;
} X86CPURegister32List;


typedef struct X86CPUFeatureWordInfo X86CPUFeatureWordInfo;

typedef struct X86CPUFeatureWordInfoList
{
    union {
        X86CPUFeatureWordInfo *value;
        uint64_t padding;
    };
    struct X86CPUFeatureWordInfoList *next;
} X86CPUFeatureWordInfoList;

extern const char *RxState_lookup[];
typedef enum RxState
{
    RX_STATE_NORMAL = 0,
    RX_STATE_NONE = 1,
    RX_STATE_ALL = 2,
    RX_STATE_MAX = 3,
} RxState;

typedef struct RxStateList
{
    RxState value;
    struct RxStateList *next;
} RxStateList;


typedef struct RxFilterInfo RxFilterInfo;

typedef struct RxFilterInfoList
{
    union {
        RxFilterInfo *value;
        uint64_t padding;
    };
    struct RxFilterInfoList *next;
} RxFilterInfoList;

#ifndef QAPI_TYPES_BUILTIN_CLEANUP_DECL_H
#define QAPI_TYPES_BUILTIN_CLEANUP_DECL_H

void qapi_free_strList(strList * obj);
void qapi_free_intList(intList * obj);
void qapi_free_numberList(numberList * obj);
void qapi_free_boolList(boolList * obj);
void qapi_free_int8List(int8List * obj);
void qapi_free_int16List(int16List * obj);
void qapi_free_int32List(int32List * obj);
void qapi_free_int64List(int64List * obj);
void qapi_free_uint8List(uint8List * obj);
void qapi_free_uint16List(uint16List * obj);
void qapi_free_uint32List(uint32List * obj);
void qapi_free_uint64List(uint64List * obj);

#endif /* QAPI_TYPES_BUILTIN_CLEANUP_DECL_H */


void qapi_free_ErrorClassList(ErrorClassList * obj);

struct NameInfo
{
    bool has_name;
    char * name;
};

void qapi_free_NameInfoList(NameInfoList * obj);
void qapi_free_NameInfo(NameInfo * obj);

struct VersionInfo
{
    struct 
    {
        int64_t major;
        int64_t minor;
        int64_t micro;
    } qemu;
    char * package;
};

void qapi_free_VersionInfoList(VersionInfoList * obj);
void qapi_free_VersionInfo(VersionInfo * obj);

struct KvmInfo
{
    bool enabled;
    bool present;
};

void qapi_free_KvmInfoList(KvmInfoList * obj);
void qapi_free_KvmInfo(KvmInfo * obj);

void qapi_free_RunStateList(RunStateList * obj);

struct SnapshotInfo
{
    char * id;
    char * name;
    int64_t vm_state_size;
    int64_t date_sec;
    int64_t date_nsec;
    int64_t vm_clock_sec;
    int64_t vm_clock_nsec;
};

void qapi_free_SnapshotInfoList(SnapshotInfoList * obj);
void qapi_free_SnapshotInfo(SnapshotInfo * obj);

struct ImageInfo
{
    char * filename;
    char * format;
    bool has_dirty_flag;
    bool dirty_flag;
    bool has_actual_size;
    int64_t actual_size;
    int64_t virtual_size;
    bool has_cluster_size;
    int64_t cluster_size;
    bool has_encrypted;
    bool encrypted;
    bool has_backing_filename;
    char * backing_filename;
    bool has_full_backing_filename;
    char * full_backing_filename;
    bool has_backing_filename_format;
    char * backing_filename_format;
    bool has_snapshots;
    SnapshotInfoList * snapshots;
    bool has_backing_image;
    ImageInfo * backing_image;
};

void qapi_free_ImageInfoList(ImageInfoList * obj);
void qapi_free_ImageInfo(ImageInfo * obj);

struct ImageCheck
{
    char * filename;
    char * format;
    int64_t check_errors;
    bool has_image_end_offset;
    int64_t image_end_offset;
    bool has_corruptions;
    int64_t corruptions;
    bool has_leaks;
    int64_t leaks;
    bool has_corruptions_fixed;
    int64_t corruptions_fixed;
    bool has_leaks_fixed;
    int64_t leaks_fixed;
    bool has_total_clusters;
    int64_t total_clusters;
    bool has_allocated_clusters;
    int64_t allocated_clusters;
    bool has_fragmented_clusters;
    int64_t fragmented_clusters;
    bool has_compressed_clusters;
    int64_t compressed_clusters;
};

void qapi_free_ImageCheckList(ImageCheckList * obj);
void qapi_free_ImageCheck(ImageCheck * obj);

struct StatusInfo
{
    bool running;
    bool singlestep;
    RunState status;
};

void qapi_free_StatusInfoList(StatusInfoList * obj);
void qapi_free_StatusInfo(StatusInfo * obj);

struct UuidInfo
{
    char * UUID;
};

void qapi_free_UuidInfoList(UuidInfoList * obj);
void qapi_free_UuidInfo(UuidInfo * obj);

struct ChardevInfo
{
    char * label;
    char * filename;
};

void qapi_free_ChardevInfoList(ChardevInfoList * obj);
void qapi_free_ChardevInfo(ChardevInfo * obj);

void qapi_free_DataFormatList(DataFormatList * obj);

struct CommandInfo
{
    char * name;
};

void qapi_free_CommandInfoList(CommandInfoList * obj);
void qapi_free_CommandInfo(CommandInfo * obj);

struct EventInfo
{
    char * name;
};

void qapi_free_EventInfoList(EventInfoList * obj);
void qapi_free_EventInfo(EventInfo * obj);

struct MigrationStats
{
    int64_t transferred;
    int64_t remaining;
    int64_t total;
    int64_t duplicate;
    int64_t skipped;
    int64_t normal;
    int64_t normal_bytes;
    int64_t dirty_pages_rate;
    double mbps;
};

void qapi_free_MigrationStatsList(MigrationStatsList * obj);
void qapi_free_MigrationStats(MigrationStats * obj);

struct XBZRLECacheStats
{
    int64_t cache_size;
    int64_t bytes;
    int64_t pages;
    int64_t cache_miss;
    int64_t overflow;
};

void qapi_free_XBZRLECacheStatsList(XBZRLECacheStatsList * obj);
void qapi_free_XBZRLECacheStats(XBZRLECacheStats * obj);

struct MigrationInfo
{
    bool has_status;
    char * status;
    bool has_ram;
    MigrationStats * ram;
    bool has_disk;
    MigrationStats * disk;
    bool has_xbzrle_cache;
    XBZRLECacheStats * xbzrle_cache;
    bool has_total_time;
    int64_t total_time;
    bool has_expected_downtime;
    int64_t expected_downtime;
    bool has_downtime;
    int64_t downtime;
};

void qapi_free_MigrationInfoList(MigrationInfoList * obj);
void qapi_free_MigrationInfo(MigrationInfo * obj);

void qapi_free_MigrationCapabilityList(MigrationCapabilityList * obj);

struct MigrationCapabilityStatus
{
    MigrationCapability capability;
    bool state;
};

void qapi_free_MigrationCapabilityStatusList(MigrationCapabilityStatusList * obj);
void qapi_free_MigrationCapabilityStatus(MigrationCapabilityStatus * obj);

struct MouseInfo
{
    char * name;
    int64_t index;
    bool current;
    bool absolute;
};

void qapi_free_MouseInfoList(MouseInfoList * obj);
void qapi_free_MouseInfo(MouseInfo * obj);

struct CpuInfo
{
    int64_t CPU;
    bool current;
    bool halted;
    bool has_pc;
    int64_t pc;
    bool has_nip;
    int64_t nip;
    bool has_npc;
    int64_t npc;
    bool has_PC;
    int64_t PC;
    int64_t thread_id;
};

void qapi_free_CpuInfoList(CpuInfoList * obj);
void qapi_free_CpuInfo(CpuInfo * obj);

struct BlockDeviceInfo
{
    char * file;
    bool ro;
    char * drv;
    bool has_backing_file;
    char * backing_file;
    int64_t backing_file_depth;
    bool encrypted;
    bool encryption_key_missing;
    int64_t bps;
    int64_t bps_rd;
    int64_t bps_wr;
    int64_t iops;
    int64_t iops_rd;
    int64_t iops_wr;
    ImageInfo * image;
};

void qapi_free_BlockDeviceInfoList(BlockDeviceInfoList * obj);
void qapi_free_BlockDeviceInfo(BlockDeviceInfo * obj);

void qapi_free_BlockDeviceIoStatusList(BlockDeviceIoStatusList * obj);

struct BlockDirtyInfo
{
    int64_t count;
    int64_t granularity;
};

void qapi_free_BlockDirtyInfoList(BlockDirtyInfoList * obj);
void qapi_free_BlockDirtyInfo(BlockDirtyInfo * obj);

struct BlockInfo
{
    char * device;
    char * type;
    bool removable;
    bool locked;
    bool has_inserted;
    BlockDeviceInfo * inserted;
    bool has_tray_open;
    bool tray_open;
    bool has_io_status;
    BlockDeviceIoStatus io_status;
    bool has_dirty;
    BlockDirtyInfo * dirty;
};

void qapi_free_BlockInfoList(BlockInfoList * obj);
void qapi_free_BlockInfo(BlockInfo * obj);

struct BlockDeviceStats
{
    int64_t rd_bytes;
    int64_t wr_bytes;
    int64_t rd_operations;
    int64_t wr_operations;
    int64_t flush_operations;
    int64_t flush_total_time_ns;
    int64_t wr_total_time_ns;
    int64_t rd_total_time_ns;
    int64_t wr_highest_offset;
};

void qapi_free_BlockDeviceStatsList(BlockDeviceStatsList * obj);
void qapi_free_BlockDeviceStats(BlockDeviceStats * obj);

struct BlockStats
{
    bool has_device;
    char * device;
    BlockDeviceStats * stats;
    bool has_parent;
    BlockStats * parent;
};

void qapi_free_BlockStatsList(BlockStatsList * obj);
void qapi_free_BlockStats(BlockStats * obj);

struct VncClientInfo
{
    char * host;
    char * family;
    char * service;
    bool has_x509_dname;
    char * x509_dname;
    bool has_sasl_username;
    char * sasl_username;
};

void qapi_free_VncClientInfoList(VncClientInfoList * obj);
void qapi_free_VncClientInfo(VncClientInfo * obj);

struct VncInfo
{
    bool enabled;
    bool has_host;
    char * host;
    bool has_family;
    char * family;
    bool has_service;
    char * service;
    bool has_auth;
    char * auth;
    bool has_clients;
    VncClientInfoList * clients;
};

void qapi_free_VncInfoList(VncInfoList * obj);
void qapi_free_VncInfo(VncInfo * obj);

struct SpiceChannel
{
    char * host;
    char * family;
    char * port;
    int64_t connection_id;
    int64_t channel_type;
    int64_t channel_id;
    bool tls;
};

void qapi_free_SpiceChannelList(SpiceChannelList * obj);
void qapi_free_SpiceChannel(SpiceChannel * obj);

void qapi_free_SpiceQueryMouseModeList(SpiceQueryMouseModeList * obj);

struct SpiceInfo
{
    bool enabled;
    bool migrated;
    bool has_host;
    char * host;
    bool has_port;
    int64_t port;
    bool has_tls_port;
    int64_t tls_port;
    bool has_auth;
    char * auth;
    bool has_compiled_version;
    char * compiled_version;
    SpiceQueryMouseMode mouse_mode;
    bool has_channels;
    SpiceChannelList * channels;
};

void qapi_free_SpiceInfoList(SpiceInfoList * obj);
void qapi_free_SpiceInfo(SpiceInfo * obj);

struct BalloonInfo
{
    int64_t actual;
};

void qapi_free_BalloonInfoList(BalloonInfoList * obj);
void qapi_free_BalloonInfo(BalloonInfo * obj);

struct PciMemoryRange
{
    int64_t base;
    int64_t limit;
};

void qapi_free_PciMemoryRangeList(PciMemoryRangeList * obj);
void qapi_free_PciMemoryRange(PciMemoryRange * obj);

struct PciMemoryRegion
{
    int64_t bar;
    char * type;
    int64_t address;
    int64_t size;
    bool has_prefetch;
    bool prefetch;
    bool has_mem_type_64;
    bool mem_type_64;
};

void qapi_free_PciMemoryRegionList(PciMemoryRegionList * obj);
void qapi_free_PciMemoryRegion(PciMemoryRegion * obj);

struct PciBridgeInfo
{
    struct 
    {
        int64_t number;
        int64_t secondary;
        int64_t subordinate;
        PciMemoryRange * io_range;
        PciMemoryRange * memory_range;
        PciMemoryRange * prefetchable_range;
    } bus;
    bool has_devices;
    PciDeviceInfoList * devices;
};

void qapi_free_PciBridgeInfoList(PciBridgeInfoList * obj);
void qapi_free_PciBridgeInfo(PciBridgeInfo * obj);

struct PciDeviceInfo
{
    int64_t bus;
    int64_t slot;
    int64_t function;
    struct 
    {
        bool has_desc;
        char * desc;
        int64_t class;
    } class_info;
    struct 
    {
        int64_t device;
        int64_t vendor;
    } id;
    bool has_irq;
    int64_t irq;
    char * qdev_id;
    bool has_pci_bridge;
    PciBridgeInfo * pci_bridge;
    PciMemoryRegionList * regions;
};

void qapi_free_PciDeviceInfoList(PciDeviceInfoList * obj);
void qapi_free_PciDeviceInfo(PciDeviceInfo * obj);

struct PciInfo
{
    int64_t bus;
    PciDeviceInfoList * devices;
};

void qapi_free_PciInfoList(PciInfoList * obj);
void qapi_free_PciInfo(PciInfo * obj);

void qapi_free_BlockdevOnErrorList(BlockdevOnErrorList * obj);

void qapi_free_MirrorSyncModeList(MirrorSyncModeList * obj);

struct BlockJobInfo
{
    char * type;
    char * device;
    int64_t len;
    int64_t offset;
    bool busy;
    bool paused;
    int64_t speed;
    BlockDeviceIoStatus io_status;
};

void qapi_free_BlockJobInfoList(BlockJobInfoList * obj);
void qapi_free_BlockJobInfo(BlockJobInfo * obj);

void qapi_free_NewImageModeList(NewImageModeList * obj);

struct BlockdevSnapshot
{
    char * device;
    char * snapshot_file;
    bool has_format;
    char * format;
    bool has_mode;
    NewImageMode mode;
};

void qapi_free_BlockdevSnapshotList(BlockdevSnapshotList * obj);
void qapi_free_BlockdevSnapshot(BlockdevSnapshot * obj);

struct DriveBackup
{
    char * device;
    char * target;
    bool has_format;
    char * format;
    MirrorSyncMode sync;
    bool has_mode;
    NewImageMode mode;
    bool has_speed;
    int64_t speed;
    bool has_on_source_error;
    BlockdevOnError on_source_error;
    bool has_on_target_error;
    BlockdevOnError on_target_error;
};

void qapi_free_DriveBackupList(DriveBackupList * obj);
void qapi_free_DriveBackup(DriveBackup * obj);

struct Abort
{
};

void qapi_free_AbortList(AbortList * obj);
void qapi_free_Abort(Abort * obj);

struct TransactionAction
{
    TransactionActionKind kind;
    union {
        void *data;
        BlockdevSnapshot * blockdev_snapshot_sync;
        DriveBackup * drive_backup;
        Abort * abort;
    };
};
void qapi_free_TransactionActionList(TransactionActionList * obj);
void qapi_free_TransactionAction(TransactionAction * obj);

struct ObjectPropertyInfo
{
    char * name;
    char * type;
};

void qapi_free_ObjectPropertyInfoList(ObjectPropertyInfoList * obj);
void qapi_free_ObjectPropertyInfo(ObjectPropertyInfo * obj);

struct ObjectTypeInfo
{
    char * name;
};

void qapi_free_ObjectTypeInfoList(ObjectTypeInfoList * obj);
void qapi_free_ObjectTypeInfo(ObjectTypeInfo * obj);

struct DevicePropertyInfo
{
    char * name;
    char * type;
};

void qapi_free_DevicePropertyInfoList(DevicePropertyInfoList * obj);
void qapi_free_DevicePropertyInfo(DevicePropertyInfo * obj);

struct NetdevNoneOptions
{
};

void qapi_free_NetdevNoneOptionsList(NetdevNoneOptionsList * obj);
void qapi_free_NetdevNoneOptions(NetdevNoneOptions * obj);

struct NetLegacyNicOptions
{
    bool has_netdev;
    char * netdev;
    bool has_macaddr;
    char * macaddr;
    bool has_model;
    char * model;
    bool has_addr;
    char * addr;
    bool has_vectors;
    uint32_t vectors;
};

void qapi_free_NetLegacyNicOptionsList(NetLegacyNicOptionsList * obj);
void qapi_free_NetLegacyNicOptions(NetLegacyNicOptions * obj);

struct String
{
    char * str;
};

void qapi_free_StringList(StringList * obj);
void qapi_free_String(String * obj);

struct NetdevUserOptions
{
    bool has_hostname;
    char * hostname;
    bool has_q_restrict;
    bool q_restrict;
    bool has_ip;
    char * ip;
    bool has_net;
    char * net;
    bool has_host;
    char * host;
    bool has_tftp;
    char * tftp;
    bool has_bootfile;
    char * bootfile;
    bool has_dhcpstart;
    char * dhcpstart;
    bool has_dns;
    char * dns;
    bool has_dnssearch;
    StringList * dnssearch;
    bool has_smb;
    char * smb;
    bool has_smbserver;
    char * smbserver;
    bool has_hostfwd;
    StringList * hostfwd;
    bool has_guestfwd;
    StringList * guestfwd;
};

void qapi_free_NetdevUserOptionsList(NetdevUserOptionsList * obj);
void qapi_free_NetdevUserOptions(NetdevUserOptions * obj);

struct NetdevTapOptions
{
    bool has_ifname;
    char * ifname;
    bool has_fd;
    char * fd;
    bool has_fds;
    char * fds;
    bool has_script;
    char * script;
    bool has_downscript;
    char * downscript;
    bool has_helper;
    char * helper;
    bool has_sndbuf;
    uint64_t sndbuf;
    bool has_vnet_hdr;
    bool vnet_hdr;
    bool has_vhost;
    bool vhost;
    bool has_vhostfd;
    char * vhostfd;
    bool has_vhostfds;
    char * vhostfds;
    bool has_vhostforce;
    bool vhostforce;
    bool has_queues;
    uint32_t queues;
};

void qapi_free_NetdevTapOptionsList(NetdevTapOptionsList * obj);
void qapi_free_NetdevTapOptions(NetdevTapOptions * obj);

struct NetdevSocketOptions
{
    bool has_fd;
    char * fd;
    bool has_listen;
    char * listen;
    bool has_connect;
    char * connect;
    bool has_mcast;
    char * mcast;
    bool has_localaddr;
    char * localaddr;
    bool has_udp;
    char * udp;
};

void qapi_free_NetdevSocketOptionsList(NetdevSocketOptionsList * obj);
void qapi_free_NetdevSocketOptions(NetdevSocketOptions * obj);

struct NetdevVdeOptions
{
    bool has_sock;
    char * sock;
    bool has_port;
    uint16_t port;
    bool has_group;
    char * group;
    bool has_mode;
    uint16_t mode;
};

void qapi_free_NetdevVdeOptionsList(NetdevVdeOptionsList * obj);
void qapi_free_NetdevVdeOptions(NetdevVdeOptions * obj);

struct NetdevDumpOptions
{
    bool has_len;
    uint64_t len;
    bool has_file;
    char * file;
};

void qapi_free_NetdevDumpOptionsList(NetdevDumpOptionsList * obj);
void qapi_free_NetdevDumpOptions(NetdevDumpOptions * obj);

struct NetdevBridgeOptions
{
    bool has_br;
    char * br;
    bool has_helper;
    char * helper;
};

void qapi_free_NetdevBridgeOptionsList(NetdevBridgeOptionsList * obj);
void qapi_free_NetdevBridgeOptions(NetdevBridgeOptions * obj);

struct NetdevHubPortOptions
{
    int32_t hubid;
};

void qapi_free_NetdevHubPortOptionsList(NetdevHubPortOptionsList * obj);
void qapi_free_NetdevHubPortOptions(NetdevHubPortOptions * obj);

struct NetClientOptions
{
    NetClientOptionsKind kind;
    union {
        void *data;
        NetdevNoneOptions * none;
        NetLegacyNicOptions * nic;
        NetdevUserOptions * user;
        NetdevTapOptions * tap;
        NetdevSocketOptions * socket;
        NetdevVdeOptions * vde;
        NetdevDumpOptions * dump;
        NetdevBridgeOptions * bridge;
        NetdevHubPortOptions * hubport;
    };
};
void qapi_free_NetClientOptionsList(NetClientOptionsList * obj);
void qapi_free_NetClientOptions(NetClientOptions * obj);

struct NetLegacy
{
    bool has_vlan;
    int32_t vlan;
    bool has_id;
    char * id;
    bool has_name;
    char * name;
    NetClientOptions * opts;
};

void qapi_free_NetLegacyList(NetLegacyList * obj);
void qapi_free_NetLegacy(NetLegacy * obj);

struct Netdev
{
    char * id;
    NetClientOptions * opts;
};

void qapi_free_NetdevList(NetdevList * obj);
void qapi_free_Netdev(Netdev * obj);

struct InetSocketAddress
{
    char * host;
    char * port;
    bool has_to;
    uint16_t to;
    bool has_ipv4;
    bool ipv4;
    bool has_ipv6;
    bool ipv6;
};

void qapi_free_InetSocketAddressList(InetSocketAddressList * obj);
void qapi_free_InetSocketAddress(InetSocketAddress * obj);

struct UnixSocketAddress
{
    char * path;
};

void qapi_free_UnixSocketAddressList(UnixSocketAddressList * obj);
void qapi_free_UnixSocketAddress(UnixSocketAddress * obj);

struct SocketAddress
{
    SocketAddressKind kind;
    union {
        void *data;
        InetSocketAddress * inet;
        UnixSocketAddress * q_unix;
        String * fd;
    };
};
void qapi_free_SocketAddressList(SocketAddressList * obj);
void qapi_free_SocketAddress(SocketAddress * obj);

struct MachineInfo
{
    char * name;
    bool has_alias;
    char * alias;
    bool has_is_default;
    bool is_default;
    int64_t cpu_max;
};

void qapi_free_MachineInfoList(MachineInfoList * obj);
void qapi_free_MachineInfo(MachineInfo * obj);

struct CpuDefinitionInfo
{
    char * name;
};

void qapi_free_CpuDefinitionInfoList(CpuDefinitionInfoList * obj);
void qapi_free_CpuDefinitionInfo(CpuDefinitionInfo * obj);

struct AddfdInfo
{
    int64_t fdset_id;
    int64_t fd;
};

void qapi_free_AddfdInfoList(AddfdInfoList * obj);
void qapi_free_AddfdInfo(AddfdInfo * obj);

struct FdsetFdInfo
{
    int64_t fd;
    bool has_opaque;
    char * opaque;
};

void qapi_free_FdsetFdInfoList(FdsetFdInfoList * obj);
void qapi_free_FdsetFdInfo(FdsetFdInfo * obj);

struct FdsetInfo
{
    int64_t fdset_id;
    FdsetFdInfoList * fds;
};

void qapi_free_FdsetInfoList(FdsetInfoList * obj);
void qapi_free_FdsetInfo(FdsetInfo * obj);

struct TargetInfo
{
    char * arch;
};

void qapi_free_TargetInfoList(TargetInfoList * obj);
void qapi_free_TargetInfo(TargetInfo * obj);

void qapi_free_QKeyCodeList(QKeyCodeList * obj);

struct KeyValue
{
    KeyValueKind kind;
    union {
        void *data;
        int64_t number;
        QKeyCode qcode;
    };
};
void qapi_free_KeyValueList(KeyValueList * obj);
void qapi_free_KeyValue(KeyValue * obj);

struct ChardevFile
{
    bool has_in;
    char * in;
    char * out;
};

void qapi_free_ChardevFileList(ChardevFileList * obj);
void qapi_free_ChardevFile(ChardevFile * obj);

struct ChardevHostdev
{
    char * device;
};

void qapi_free_ChardevHostdevList(ChardevHostdevList * obj);
void qapi_free_ChardevHostdev(ChardevHostdev * obj);

struct ChardevSocket
{
    SocketAddress * addr;
    bool has_server;
    bool server;
    bool has_wait;
    bool wait;
    bool has_nodelay;
    bool nodelay;
    bool has_telnet;
    bool telnet;
};

void qapi_free_ChardevSocketList(ChardevSocketList * obj);
void qapi_free_ChardevSocket(ChardevSocket * obj);

struct ChardevUdp
{
    SocketAddress * remote;
    bool has_local;
    SocketAddress * local;
};

void qapi_free_ChardevUdpList(ChardevUdpList * obj);
void qapi_free_ChardevUdp(ChardevUdp * obj);

struct ChardevMux
{
    char * chardev;
};

void qapi_free_ChardevMuxList(ChardevMuxList * obj);
void qapi_free_ChardevMux(ChardevMux * obj);

struct ChardevStdio
{
    bool has_signal;
    bool signal;
};

void qapi_free_ChardevStdioList(ChardevStdioList * obj);
void qapi_free_ChardevStdio(ChardevStdio * obj);

struct ChardevSpiceChannel
{
    char * type;
};

void qapi_free_ChardevSpiceChannelList(ChardevSpiceChannelList * obj);
void qapi_free_ChardevSpiceChannel(ChardevSpiceChannel * obj);

struct ChardevSpicePort
{
    char * fqdn;
};

void qapi_free_ChardevSpicePortList(ChardevSpicePortList * obj);
void qapi_free_ChardevSpicePort(ChardevSpicePort * obj);

struct ChardevVC
{
    bool has_width;
    int64_t width;
    bool has_height;
    int64_t height;
    bool has_cols;
    int64_t cols;
    bool has_rows;
    int64_t rows;
};

void qapi_free_ChardevVCList(ChardevVCList * obj);
void qapi_free_ChardevVC(ChardevVC * obj);

struct ChardevMemory
{
    bool has_size;
    int64_t size;
};

void qapi_free_ChardevMemoryList(ChardevMemoryList * obj);
void qapi_free_ChardevMemory(ChardevMemory * obj);

struct ChardevDummy
{
};

void qapi_free_ChardevDummyList(ChardevDummyList * obj);
void qapi_free_ChardevDummy(ChardevDummy * obj);

struct ChardevBackend
{
    ChardevBackendKind kind;
    union {
        void *data;
        ChardevFile * file;
        ChardevHostdev * serial;
        ChardevHostdev * parallel;
        ChardevHostdev * pipe;
        ChardevSocket * socket;
        ChardevUdp * udp;
        ChardevDummy * pty;
        ChardevDummy * null;
        ChardevMux * mux;
        ChardevDummy * msmouse;
        ChardevDummy * braille;
        ChardevStdio * stdio;
        ChardevDummy * console;
        ChardevSpiceChannel * spicevmc;
        ChardevSpicePort * spiceport;
        ChardevVC * vc;
        ChardevMemory * memory;
    };
};
void qapi_free_ChardevBackendList(ChardevBackendList * obj);
void qapi_free_ChardevBackend(ChardevBackend * obj);

struct ChardevReturn
{
    bool has_pty;
    char * pty;
};

void qapi_free_ChardevReturnList(ChardevReturnList * obj);
void qapi_free_ChardevReturn(ChardevReturn * obj);

void qapi_free_TpmModelList(TpmModelList * obj);

void qapi_free_TpmTypeList(TpmTypeList * obj);

struct TPMPassthroughOptions
{
    bool has_path;
    char * path;
    bool has_cancel_path;
    char * cancel_path;
};

void qapi_free_TPMPassthroughOptionsList(TPMPassthroughOptionsList * obj);
void qapi_free_TPMPassthroughOptions(TPMPassthroughOptions * obj);

struct TpmTypeOptions
{
    TpmTypeOptionsKind kind;
    union {
        void *data;
        TPMPassthroughOptions * passthrough;
    };
};
void qapi_free_TpmTypeOptionsList(TpmTypeOptionsList * obj);
void qapi_free_TpmTypeOptions(TpmTypeOptions * obj);

struct TPMInfo
{
    char * id;
    TpmModel model;
    TpmTypeOptions * options;
};

void qapi_free_TPMInfoList(TPMInfoList * obj);
void qapi_free_TPMInfo(TPMInfo * obj);

struct AcpiTableOptions
{
    bool has_sig;
    char * sig;
    bool has_rev;
    uint8_t rev;
    bool has_oem_id;
    char * oem_id;
    bool has_oem_table_id;
    char * oem_table_id;
    bool has_oem_rev;
    uint32_t oem_rev;
    bool has_asl_compiler_id;
    char * asl_compiler_id;
    bool has_asl_compiler_rev;
    uint32_t asl_compiler_rev;
    bool has_file;
    char * file;
    bool has_data;
    char * data;
};

void qapi_free_AcpiTableOptionsList(AcpiTableOptionsList * obj);
void qapi_free_AcpiTableOptions(AcpiTableOptions * obj);

void qapi_free_CommandLineParameterTypeList(CommandLineParameterTypeList * obj);

struct CommandLineParameterInfo
{
    char * name;
    CommandLineParameterType type;
    bool has_help;
    char * help;
};

void qapi_free_CommandLineParameterInfoList(CommandLineParameterInfoList * obj);
void qapi_free_CommandLineParameterInfo(CommandLineParameterInfo * obj);

struct CommandLineOptionInfo
{
    char * option;
    CommandLineParameterInfoList * parameters;
};

void qapi_free_CommandLineOptionInfoList(CommandLineOptionInfoList * obj);
void qapi_free_CommandLineOptionInfo(CommandLineOptionInfo * obj);

void qapi_free_X86CPURegister32List(X86CPURegister32List * obj);

struct X86CPUFeatureWordInfo
{
    int64_t cpuid_input_eax;
    bool has_cpuid_input_ecx;
    int64_t cpuid_input_ecx;
    X86CPURegister32 cpuid_register;
    int64_t features;
};

void qapi_free_X86CPUFeatureWordInfoList(X86CPUFeatureWordInfoList * obj);
void qapi_free_X86CPUFeatureWordInfo(X86CPUFeatureWordInfo * obj);

void qapi_free_RxStateList(RxStateList * obj);

struct RxFilterInfo
{
    char * name;
    bool promiscuous;
    RxState multicast;
    RxState unicast;
    bool broadcast_allowed;
    bool multicast_overflow;
    bool unicast_overflow;
    char * main_mac;
    intList * vlan_table;
    strList * unicast_table;
    strList * multicast_table;
};

void qapi_free_RxFilterInfoList(RxFilterInfoList * obj);
void qapi_free_RxFilterInfo(RxFilterInfo * obj);

#endif
