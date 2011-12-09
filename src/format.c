/*
 * Rufus: The Reliable USB Formatting Utility
 * Formatting function calls
 * Copyright (c) 2011 Pete Batard <pete@akeo.ie>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifdef _CRTDBG_MAP_ALLOC
#include <stdlib.h>
#include <crtdbg.h>
#endif

#include <windows.h>
#include <windowsx.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <process.h>
#include <stddef.h>

#include "msapi_utf8.h"
#include "rufus.h"
#include "resource.h"
#include "br.h"
#include "fat16.h"
#include "fat32.h"
#include "file.h"
#include "format.h"
#include "badblocks.h"

/*
 * Globals
 */
DWORD FormatStatus;
badblocks_report report;
static float format_percent = 0.0f;
static int task_number = 0;
/* Number of steps for each FS for FCC_STRUCTURE_PROGRESS */
const int nb_steps[FS_MAX] = { 5, 5, 12, 10 };
static int fs_index = 0;

/*
 * FormatEx callback. Return FALSE to halt operations
 */
static BOOLEAN __stdcall FormatExCallback(FILE_SYSTEM_CALLBACK_COMMAND Command, DWORD Action, PVOID pData)
{
	DWORD* percent;
	if (IS_ERROR(FormatStatus))
		return FALSE;

	switch(Command) {
	case FCC_PROGRESS:
		// TODO: send this percentage to the status bar
		percent = (DWORD*)pData;
		PrintStatus(0, "Formatting: %d%% completed.\n", *percent);
//		uprintf("%d percent completed.\n", *percent);
		UpdateProgress(OP_FORMAT, 1.0f * (*percent));
		break;
	case FCC_STRUCTURE_PROGRESS:	// No progress on quick format
		PrintStatus(0, "Creating file system: Task %d/%d completed.\n", ++task_number, nb_steps[fs_index]);
		uprintf("Create FS: Task %d/%d completed.\n", task_number, nb_steps[fs_index]);
		format_percent += 100.0f / (1.0f * nb_steps[fs_index]);
		UpdateProgress(OP_CREATE_FS, format_percent);
		break;
	case FCC_DONE:
		PrintStatus(0, "Creating file system: Task %d/%d completed.\n", nb_steps[fs_index], nb_steps[fs_index]);
		uprintf("Create FS: Task %d/%d completed.\n", nb_steps[fs_index], nb_steps[fs_index]);
		UpdateProgress(OP_CREATE_FS, 100.0f);
		if(*(BOOLEAN*)pData == FALSE) {
			uprintf("Error while formatting.\n");
			FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_GEN_FAILURE;
		}
		break;
	case FCC_DONE_WITH_STRUCTURE:	// We get this message when formatting Small FAT16
		// pData Seems to be a struct with at least one (32 BIT!!!) string pointer to the size in MB
		uprintf("Done with that sort of things: Action=%d pData=%0p\n", Action, pData);
		DumpBufferHex(pData, 8);
		uprintf("Volume size: %s MB\n", (char*)(LONG_PTR)(*(ULONG32*)pData));
		break;
	case FCC_INCOMPATIBLE_FILE_SYSTEM:
		uprintf("Incompatible File System\n");
		FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|APPERR(ERROR_INCOMPATIBLE_FS);
		break;
	case FCC_ACCESS_DENIED:
		uprintf("Access denied\n");
		FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_ACCESS_DENIED;
		break;
	case FCC_MEDIA_WRITE_PROTECTED:
		uprintf("Media is write protected\n");
		FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_WRITE_PROTECT;
		break;
	case FCC_VOLUME_IN_USE:
		uprintf("Volume is in use\n");
		FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_DEVICE_IN_USE;
		break;
	case FCC_CANT_QUICK_FORMAT:
		uprintf("Cannot quick format this volume\n");
		FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|APPERR(ERROR_CANT_QUICK_FORMAT);
		break;
	case FCC_BAD_LABEL:
		uprintf("Bad label\n");
		FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_LABEL_TOO_LONG;
		break;
	case FCC_OUTPUT:
		uprintf("%s\n", ((PTEXTOUTPUT)pData)->Output);
		break;
	case FCC_CLUSTER_SIZE_TOO_BIG:
	case FCC_CLUSTER_SIZE_TOO_SMALL:
		uprintf("Unsupported cluster size\n");
		FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|APPERR(ERROR_INVALID_CLUSTER_SIZE);
		break;
	case FCC_VOLUME_TOO_BIG:
	case FCC_VOLUME_TOO_SMALL:
		uprintf("Volume is too %s\n", FCC_VOLUME_TOO_BIG?"big":"small");
		FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|APPERR(ERROR_INVALID_VOLUME_SIZE);
	case FCC_NO_MEDIA_IN_DRIVE:
		uprintf("No media in drive\n");
		FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_NO_MEDIA_IN_DRIVE;
		break;
	default:
		uprintf("FormatExCallback: received unhandled command %X\n", Command);
		FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_NOT_SUPPORTED;
		break;
	}
	return (!IS_ERROR(FormatStatus));
}

/*
 * Call on fmifs.dll's FormatEx() to format the drive
 */
static BOOL FormatDrive(char DriveLetter)
{
	BOOL r = FALSE;
	PF_DECL(FormatEx);
	WCHAR wDriveRoot[] = L"?:\\";
	WCHAR wFSType[32];
	WCHAR wLabel[128];
	size_t i;

	wDriveRoot[0] = (WCHAR)DriveLetter;
	PrintStatus(0, "Formatting...");
	PF_INIT_OR_OUT(FormatEx, fmifs);

	GetWindowTextW(hFileSystem, wFSType, ARRAYSIZE(wFSType));
	// We may have a " (Default)" trail
	for (i=0; i<wcslen(wFSType); i++) {
		if (wFSType[i] == ' ') {
			wFSType[i] = 0;
			break;
		}
	}
	GetWindowTextW(hLabel, wLabel, ARRAYSIZE(wLabel));
	uprintf("Using cluster size: %d bytes\n", ComboBox_GetItemData(hClusterSize, ComboBox_GetCurSel(hClusterSize)));
	format_percent = 0.0f;
	task_number = 0;
	fs_index = ComboBox_GetItemData(hFileSystem, ComboBox_GetCurSel(hFileSystem));
	pfFormatEx(wDriveRoot, SelectedDrive.Geometry.MediaType, wFSType, wLabel,
		IsChecked(IDC_QUICKFORMAT), (ULONG)ComboBox_GetItemData(hClusterSize, ComboBox_GetCurSel(hClusterSize)),
		FormatExCallback);
	if (!IS_ERROR(FormatStatus)) {
		uprintf("Format completed.\n");
		r = TRUE;
	}

out:
	return r;
}

static BOOL AnalyzeMBR(HANDLE hPhysicalDrive)
{
	FILE fake_fd;

	fake_fd._ptr = (char*)hPhysicalDrive;
	fake_fd._bufsiz = SelectedDrive.Geometry.BytesPerSector;

	// TODO: Apply this detection before partitioning
	if (is_br(&fake_fd)) {
		uprintf("Drive has an x86 boot sector\n");
	} else{
		uprintf("Drive is missing an x86 boot sector!\n");
		return FALSE;
	}
	// TODO: Add/Eliminate FAT12?
	if (is_fat_16_br(&fake_fd) || is_fat_32_br(&fake_fd)) {
		if (entire_fat_16_br_matches(&fake_fd)) {
			uprintf("Exact FAT16 DOS boot record match\n");
		} else if (entire_fat_16_fd_br_matches(&fake_fd)) {
			uprintf("Exact FAT16 FreeDOS boot record match\n");
		} else if (entire_fat_32_br_matches(&fake_fd)) {
			uprintf("Exact FAT32 DOS boot record match\n");
		} else if (entire_fat_32_nt_br_matches(&fake_fd)) {
			uprintf("Exact FAT32 NT boot record match\n");
		} else if (entire_fat_32_fd_br_matches(&fake_fd)) {
			uprintf("Exactly FAT32 FreeDOS boot record match\n");
		} else {
			uprintf("Unknown FAT16 or FAT32 boot record\n");
		}
	} else if (is_dos_mbr(&fake_fd)) {
		uprintf("Microsoft DOS/NT/95A master boot record match\n");
	} else if (is_dos_f2_mbr(&fake_fd)) {
		uprintf("Microsoft DOS/NT/95A master boot record with the undocumented\n");
		uprintf("F2 instruction match\n");
	} else if (is_95b_mbr(&fake_fd)) {
		uprintf("Microsoft 95B/98/98SE/ME master boot record match\n");
	} else if (is_2000_mbr(&fake_fd)) {
		uprintf("Microsoft 2000/XP/2003 master boot record match\n");
	} else if (is_vista_mbr(&fake_fd)) {
		uprintf("Microsoft Vista master boot record match\n");
	} else if (is_win7_mbr(&fake_fd)) {
		uprintf("Microsoft 7 master boot record match\n");
	} else if (is_zero_mbr(&fake_fd)) {
		uprintf("Zeroed non-bootable master boot record match\n");
	} else {
		uprintf("Unknown boot record\n");
	}
	return TRUE;
}


static BOOL ClearMBR(HANDLE hPhysicalDrive)
{
	FILE fake_fd;

	fake_fd._ptr = (char*)hPhysicalDrive;
	fake_fd._bufsiz = SelectedDrive.Geometry.BytesPerSector;
	return clear_mbr(&fake_fd);
}

/*
 * Process the Master Boot Record
 */
static BOOL WriteMBR(HANDLE hPhysicalDrive)
{
	BOOL r = FALSE;
	unsigned char* buf = NULL;
	size_t SecSize = SelectedDrive.Geometry.BytesPerSector;
	size_t nSecs = (0x200 + SecSize -1) / SecSize;
	FILE fake_fd;

	if (!AnalyzeMBR(hPhysicalDrive)) return FALSE;

	// FormatEx rewrites the MBR and removes the LBA attribute of FAT16
	// and FAT32 partitions - we need to correct this in the MBR
	// TODO: something else for bootable GPT
	buf = (unsigned char*)malloc(SecSize * nSecs);
	if (buf == NULL) {
		uprintf("Could not allocate memory for MBR");
		FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_NOT_ENOUGH_MEMORY;
		goto out;
	}

	if (!read_sectors(hPhysicalDrive, SelectedDrive.Geometry.BytesPerSector, 0, nSecs, buf)) {
		uprintf("Could not read MBR\n");
		FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_READ_FAULT;
		goto out;
	}

	switch (ComboBox_GetItemData(hFileSystem, ComboBox_GetCurSel(hFileSystem))) {
	case FS_FAT16:
		if (buf[0x1c2] == 0x0e) {
			uprintf("Partition is already FAT16 LBA...\n");
		} else if ((buf[0x1c2] != 0x04) && (buf[0x1c2] != 0x06)) {
			uprintf("Warning: converting a non FAT16 partition to FAT16 LBA: FS type=0x%02x\n", buf[0x1c2]);
		}
		buf[0x1c2] = 0x0e;
		break;
	case FS_FAT32:
		if (buf[0x1c2] == 0x0c) {
			uprintf("Partition is already FAT32 LBA...\n");
		} else if (buf[0x1c2] != 0x0b) {
			uprintf("Warning: converting a non FAT32 partition to FAT32 LBA: FS type=0x%02x\n", buf[0x1c2]);
		}
		buf[0x1c2] = 0x0c;
		break;
	}
	if (IsChecked(IDC_DOS)) {
		buf[0x1be] = 0x80;		// Set first partition bootable
	}

	if (!write_sectors(hPhysicalDrive, SecSize, 0, nSecs, buf)) {
		uprintf("Could not write MBR\n");
		FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_WRITE_FAULT;
		goto out;
	}

	fake_fd._ptr = (char*)hPhysicalDrive;
	fake_fd._bufsiz = SelectedDrive.Geometry.BytesPerSector;
	r = write_95b_mbr(&fake_fd);

out:
	safe_free(buf);
	return r;
}

/*
 * Process the Partition Boot Record
 */
static BOOL WritePBR(HANDLE hLogicalVolume)
{
	FILE fake_fd;

	fake_fd._ptr = (char*)hLogicalVolume;
	fake_fd._bufsiz = SelectedDrive.Geometry.BytesPerSector;

	switch (ComboBox_GetItemData(hFileSystem, ComboBox_GetCurSel(hFileSystem))) {
	case FS_FAT16:
		if (write_fat_16_br(&fake_fd, 0))
			return TRUE;
	case FS_FAT32:
		if (write_fat_32_br(&fake_fd, 0))
			return TRUE;
	default:
		uprintf("unsupported FS for FS BR processing\n");
		break;
	}
	FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_WRITE_FAULT;
	return FALSE;
}

/*
 * Standalone thread for the formatting operation
 */
void __cdecl FormatThread(void* param)
{
	DWORD num = (DWORD)(uintptr_t)param;
	HANDLE hPhysicalDrive = INVALID_HANDLE_VALUE;
	HANDLE hLogicalVolume = INVALID_HANDLE_VALUE;
	char drive_name[] = "?:";
	char bb_msg[256];
	int i;

	hPhysicalDrive = GetDriveHandle(num, NULL, TRUE, TRUE);
	if (hPhysicalDrive == INVALID_HANDLE_VALUE) {
		FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_OPEN_FAILED;
		goto out;
	}
	// At this stage with have both a handle and a lock to the physical drive...

	if (IsChecked(IDC_BADBLOCKS)) {
		// ... but we can't write sectors that are part of a volume, even if we have 
		// access to physical, unless we have a lock (which doesn't have to be write)
		hLogicalVolume = GetDriveHandle(num, drive_name, FALSE, TRUE);
		if (hLogicalVolume == INVALID_HANDLE_VALUE) {
			uprintf("Could not lock volume for badblock checks\n");
			FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_OPEN_FAILED;
			goto out;
		}
bb_retry:
		if (!BadBlocks(hPhysicalDrive, SelectedDrive.DiskSize,
			SelectedDrive.Geometry.BytesPerSector, BADBLOCKS_RW, &report)) {
			uprintf("Bad blocks check failed.\n");
			if (!FormatStatus)
				FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|
					APPERR(ERROR_BADBLOCKS_FAILURE);
			// TODO: should probably ClearMBR here as well
			goto out;
		}
		uprintf("Check completed, %u bad block%s found. (%d/%d/%d errors)\n",
			report.bb_count, (report.bb_count==1)?"":"s",
			report.num_read_errors, report.num_write_errors, report.num_corruption_errors);
		if (report.bb_count) {
			safe_sprintf(bb_msg, sizeof(bb_msg), "Check completed - %u bad block%s found:\n"
				"  %d read errors\n  %d write errors\n  %d corruption errors",
				report.bb_count, (report.bb_count==1)?"":"s",
				report.num_read_errors, report.num_write_errors, 
				report.num_corruption_errors);
			switch(MessageBoxA(hMainDialog, bb_msg, "Bad blocks check", 
				MB_ABORTRETRYIGNORE|MB_ICONWARNING)) {
			case IDRETRY: 
				goto bb_retry;
			case IDABORT:
				FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_CANCELLED;
				goto out;
			}
		}
		safe_unlockclose(hLogicalVolume);
	}

	// Especially after destructive badblocks test, you must zero the MBR completely
	// before repartitioning. Else, all kind of bad things happen
	if (!ClearMBR(hPhysicalDrive)) {
		uprintf("unable to zero MBR\n");
		FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_WRITE_FAULT;
		goto out;
	} 
	UpdateProgress(OP_ZERO_MBR, -1.0f);

	if (!CreatePartition(hPhysicalDrive)) {
		FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_PARTITION_FAILURE;
		goto out;
	}
	UpdateProgress(OP_PARTITION, -1.0f);

	// Make sure we can access the volume again before trying to format it
	for (i=0; i<10; i++) {
		Sleep(500);
		hLogicalVolume = GetDriveHandle(num, drive_name, FALSE, FALSE);
		if (hLogicalVolume != INVALID_HANDLE_VALUE) {
			break;
		}
	}
	if (i >= 10) {
		uprintf("Could not access volume after partitioning\n");
		FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_OPEN_FAILED;
		goto out;
	}
	// Handle needs to be closed for FormatEx to be happy - we keep a lock though
	safe_closehandle(hLogicalVolume);

	if (!FormatDrive(drive_name[0])) {
		// Error will be set by FormatDrive() in FormatStatus
		uprintf("Format error: %s\n", StrError(FormatStatus));
		goto out;
	}

	// TODO: Enable compression on NTFS
	// TODO: optionally disable indexing on NTFS
	// TODO: use progress bar during MBR/FSBR/MSDOS copy
	// TODO: unlock/remount trick to make the volume reappear

	PrintStatus(0, "Writing master boot record...\n");
	if (!WriteMBR(hPhysicalDrive)) {
		// Errorcode has already been set
		goto out;
	}
	UpdateProgress(OP_FIX_MBR, -1.0f);

	if (IsChecked(IDC_DOS)) {
		// We must have a lock to modify the volume boot record...
		hLogicalVolume = GetDriveHandle(num, drive_name, TRUE, TRUE);
		if (hLogicalVolume == INVALID_HANDLE_VALUE) {
			uprintf("Could not re-mount volume for partition boot record access\n");
			FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_OPEN_FAILED;
			goto out;
		}
		PrintStatus(0, "Writing partition boot record...\n");
		if (!WritePBR(hLogicalVolume)) {
			// Errorcode has already been set
			goto out;
		}
		// ... and we must have relinquished that lock to write the MS-DOS files 
		safe_unlockclose(hLogicalVolume);
		UpdateProgress(OP_DOS, -1.0f);
		PrintStatus(0, "Copying MS-DOS files...\n");
		if (!ExtractMSDOS(drive_name)) {
			FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_CANNOT_COPY;
			goto out;
		}
	}

out:
	safe_unlockclose(hLogicalVolume);
	safe_unlockclose(hPhysicalDrive);
	PostMessage(hMainDialog, UM_FORMAT_COMPLETED, 0, 0);
	_endthread();
}
