/*
 *  Copyright(C) 2019 Canonical Ltd.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301,
 *  USA.
 */

#include <efi.h>
#include <efilib.h>

#define EFI_SHELL_PARAMETERS_PROTOCOL_GUID \
  { \
  0x752f3136, 0x4e16, 0x4fdc, { 0xa2, 0x2a, 0xe5, 0xf4, 0x68, 0x12, 0xf4, 0xca } \
  }

typedef VOID *SHELL_FILE_HANDLE;
typedef struct _EFI_SHELL_PARAMETERS_PROTOCOL {
  CHAR16 **Argv;
  UINTN Argc;
  SHELL_FILE_HANDLE StdIn;
  SHELL_FILE_HANDLE StdOut;
  SHELL_FILE_HANDLE StdErr;
} EFI_SHELL_PARAMETERS_PROTOCOL;

EFI_STATUS
efi_main (EFI_HANDLE image, EFI_SYSTEM_TABLE *systab)
{
	EFI_LOADED_IMAGE *loaded_image = NULL;
	EFI_DEVICE_PATH_PROTOCOL *FilePath;
	EFI_STATUS status;
    	EFI_HANDLE image_handle = NULL;
  	static const EFI_GUID EfiShellParametersProtocolGuid
		= EFI_SHELL_PARAMETERS_PROTOCOL_GUID;
	EFI_SHELL_PARAMETERS_PROTOCOL *EfiShellParametersProtocol = NULL;

	InitializeLib(image, systab);
	status = uefi_call_wrapper(BS->HandleProtocol,
				3,
				image, 
				&LoadedImageProtocol, 
				(void **) &loaded_image);
	if (EFI_ERROR(status)) {
		Print(L"HandleProtocol: %r\n", status);
		return status;
	}

	FilePath = FileDevicePath(loaded_image->DeviceHandle, L"efi\\ubuntu\\UbuntuSecBoot.efi");
	Print(L"file path         : %s\n", DevicePathToStr(FilePath));

	status = uefi_call_wrapper(BS->LoadImage,
				6,
				FALSE,
				image,
				FilePath,
				NULL,
				0,
				&image_handle);
	if (EFI_ERROR(status)) {
		Print(L"Load Image Status = %x\n", status);
		return status;
	}

	// Install EFI_SHELL_PARAMETERS_PROTOCOL for the application
	// which will be loaded
	EfiShellParametersProtocol = AllocateZeroPool(sizeof(EFI_SHELL_PARAMETERS_PROTOCOL));
	EfiShellParametersProtocol->Argc = 2;
	EfiShellParametersProtocol->Argv[0] = L"UbuntuSecBoot.efi";
	EfiShellParametersProtocol->Argv[1] = L"-enable";

	status = uefi_call_wrapper(BS->InstallProtocolInterface,
					4,
					&image_handle,
					&EfiShellParametersProtocolGuid,
					EFI_NATIVE_INTERFACE,
					(void *)EfiShellParametersProtocol);

	if (EFI_ERROR(status)) {
		Print(L"Install protocol Status = %x\n", status);
		return status;
	}

	status = uefi_call_wrapper(BS->StartImage,
				3,
				image_handle,
				NULL,
				NULL);
	if (!EFI_ERROR(status)) {
		Print(L"StartImage success\n");
	}

	uefi_call_wrapper(systab->BootServices->Stall, 1, 10000000);

	uefi_call_wrapper(RT->ResetSystem, 4,
		EfiResetWarm,
		EFI_SUCCESS,
		0,
		NULL);

	return EFI_SUCCESS;
}
