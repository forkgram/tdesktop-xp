/*
 * winapifamily.h - minimal backfill for the Windows 7 SDK (7.1A), which
 * predates the WINAPI_FAMILY partitioning introduced with the Windows 8 SDK.
 *
 * Only files that explicitly #include <winapifamily.h> (e.g. VersionHelpers.h)
 * pick this up; the 7.1A SDK headers never reference these macros, so adding
 * this file does not change their behaviour. We are always a classic desktop
 * application, so the DESKTOP/SYSTEM partitions are selected.
 */
#ifndef _INC_WINAPIFAMILY
#define _INC_WINAPIFAMILY

#ifdef _MSC_VER
#pragma once
#endif

#define WINAPI_PARTITION_DESKTOP    0x00000001
#define WINAPI_PARTITION_APP        0x00000002
#define WINAPI_PARTITION_PHONE_APP  0x00000020
#define WINAPI_PARTITION_SYSTEM     0x00000200
#define WINAPI_PARTITION_GAMES      0x00000400

#define WINAPI_FAMILY_PC_APP        2
#define WINAPI_FAMILY_PHONE_APP     3
#define WINAPI_FAMILY_SYSTEM        4
#define WINAPI_FAMILY_SERVER        5
#define WINAPI_FAMILY_GAMES         6
#define WINAPI_FAMILY_DESKTOP_APP   100
#define WINAPI_FAMILY_APP           WINAPI_FAMILY_PC_APP

#ifndef WINAPI_FAMILY
#define WINAPI_FAMILY               WINAPI_FAMILY_DESKTOP_APP
#endif

/*
 * Desktop application family enables the DESKTOP, APP and SYSTEM partitions.
 * WINAPI_FAMILY_PARTITION(set) is non-zero when the requested partition set
 * intersects the enabled partitions.
 */
#define WINAPI_FAMILY_PARTITION(Partitions) \
    (((WINAPI_PARTITION_DESKTOP | WINAPI_PARTITION_APP | WINAPI_PARTITION_SYSTEM | WINAPI_PARTITION_GAMES) & (Partitions)) != 0)

#define WINAPI_FAMILY_ONE_PARTITION(PartitionSet, Partition) \
    (((PartitionSet) & (Partition)) != 0)

#endif /* _INC_WINAPIFAMILY */
