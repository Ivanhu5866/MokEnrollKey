/*
 *  Copyright(C) 2018 Canonical Ltd.
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

#include "authvardef.h"

#define  MOK_GUID    \
{ 0x605DAB50, 0xE046, 0x4300, {0xab, 0xb6, 0x3d, 0xd8, 0x10, 0xdd, 0x8b, 0x23}}

#define MOKKEY_DER_ENROLL_GUID   \
{ 0xe22021f7, 0x3a03, 0x4aea, {0x8b, 0x4c, 0x65, 0x88, 0x1a, 0x2b, 0x88, 0x81}}

#define MOKKEY_TEST_KERNEL_GUID   \
{ 0x161a47b3, 0xc116, 0x4942, {0xae, 0x30, 0xcd, 0xe3, 0x1e, 0xca, 0xe2, 0x42}}

//21e2d0b5-ea3a-4222-85e6-8106ad766df0
#define MOKKEY_SB_ENABLE_GUID   \
{ 0x21e2d0b5, 0xea3a, 0x4222, {0x85, 0xe6, 0x81, 0x06, 0xad, 0x76, 0x6d, 0xf0}}


#define GNVN_BUF_SIZE 1024

EFI_GUID empty_guid = {0x0,0x0,0x0,{0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0}};
EFI_GUID global_variable_guid = EFI_GLOBAL_VARIABLE;

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

static inline int
guid_cmp(EFI_GUID *a, EFI_GUID *b)
{
	return CompareMem(a, b, sizeof (*a));
}

static EFI_STATUS
read_variable(CHAR16 *name, EFI_GUID guid, void **buf_out, UINTN *buf_size_out,
	      UINT32 *attributes_out)
{
	EFI_STATUS rc;
	UINT32 attributes;
	UINTN size = 0;
	void *buf = NULL;

	rc = uefi_call_wrapper(RT->GetVariable, 5, name,
			       &guid, &attributes, &size, NULL);
	if (EFI_ERROR(rc)) {
		if (rc == EFI_BUFFER_TOO_SMALL) {
			buf = AllocatePool(size);
			if (!buf) {
				Print(L"Tried to allocate %d\n", size);
				Print(L"Could not allocate memory.\n");
				return EFI_OUT_OF_RESOURCES;
			}
		} else if (rc != EFI_NOT_FOUND) {
			Print(L"Could not get variable \"%s\": %r\n", name, rc);
			return rc;
		}
	} else {
		Print(L"GetVariable(%s) succeeded with size=0.\n", name);
		return EFI_INVALID_PARAMETER;
	}
	rc = uefi_call_wrapper(RT->GetVariable, 5, name, &guid, &attributes,
			       &size, buf);
	if (EFI_ERROR(rc)) {
		Print(L"Could not get variable \"%s\": %r\n", name, rc);
		FreePool(buf);
		return rc;
	}
	*buf_out = buf;
	*buf_size_out = size;
	*attributes_out = attributes;
	return EFI_SUCCESS;
}

static EFI_STATUS
delete_variable(CHAR16 *name, EFI_GUID guid, UINT32 attributes)
{
	return uefi_call_wrapper(RT->SetVariable, 5, name, &guid, attributes,
				 0, NULL);
}

static EFI_STATUS
delete_boot_entry(void)
{
	EFI_STATUS rc;

	UINTN variable_name_allocation = GNVN_BUF_SIZE;
	UINTN variable_name_size = 0;
	CHAR16 *variable_name;
	EFI_GUID vendor_guid = empty_guid;
	EFI_STATUS ret = EFI_OUT_OF_RESOURCES;

	variable_name = AllocateZeroPool(GNVN_BUF_SIZE * 2);
	if (!variable_name) {
		Print(L"Tried to allocate %d\n", GNVN_BUF_SIZE * 2);
		Print(L"Could not allocate memory.\n");
		return EFI_OUT_OF_RESOURCES;
	}

	while (1) {
		variable_name_size = variable_name_allocation;
		rc = uefi_call_wrapper(RT->GetNextVariableName, 3,
				       &variable_name_size, variable_name,
				       &vendor_guid);
		if (rc == EFI_BUFFER_TOO_SMALL) {

			UINTN new_allocation;
			CHAR16 *new_name;

			new_allocation = variable_name_size;
			new_name = AllocatePool(new_allocation * 2);
			if (!new_name) {
				Print(L"Tried to allocate %d\n",
				      new_allocation * 2);
				Print(L"Could not allocate memory.\n");
				ret = EFI_OUT_OF_RESOURCES;
				goto err;
			}
			CopyMem(new_name, variable_name,
				variable_name_allocation);
			variable_name_allocation = new_allocation;
			FreePool(variable_name);
			variable_name = new_name;
			continue;
		} else if (rc == EFI_NOT_FOUND) {
			break;
		} else if (EFI_ERROR(rc)) {
			Print(L"Could not get variable name: %r\n", rc);
			ret = rc;
			goto err;
		}

		/* check if the variable name is Boot#### */
		UINTN vns = StrLen(variable_name);
		if (!guid_cmp(&vendor_guid, &global_variable_guid)
		    && vns == 8 && CompareMem(variable_name, L"Boot", 8) == 0) {
			UINTN info_size = 0;
			UINT32 attributes = 0;
			void *info_ptr = NULL;
			CHAR16 *description = NULL;
			CHAR16 target[] = L"mok_enroll_key";

			rc = read_variable(variable_name, vendor_guid,
					   &info_ptr, &info_size, &attributes);
			if (EFI_ERROR(rc)) {
				ret = rc;
				goto err;
			}

			/*
			 * check if the boot path created by mok_enroll_key script.
			 */
			description = (CHAR16 *) ((UINT8 *)info_ptr
					 + sizeof(UINT32)
					 + sizeof(UINT16));

			if (CompareMem(description, target,
				       sizeof(target) - 2) == 0) {

				rc = delete_variable(variable_name,
						     vendor_guid, attributes);
				if (EFI_ERROR(rc)) {
					Print(L"Failed to delete the mok_enroll_key boot entry.\n");
					FreePool(info_ptr);
					ret = rc;
					goto err;
				}

				FreePool(info_ptr);
				break;
			}

			FreePool(info_ptr);
		}
	}

	ret = EFI_SUCCESS;
err:
	FreePool(variable_name);
	return ret;
}

EFI_STATUS
sb_enable (EFI_HANDLE image)
{
	EFI_LOADED_IMAGE *loaded_image = NULL;
	EFI_DEVICE_PATH_PROTOCOL *FilePath;
	EFI_STATUS status;
    	EFI_HANDLE image_handle = NULL;
  	static const EFI_GUID EfiShellParametersProtocolGuid
		= EFI_SHELL_PARAMETERS_PROTOCOL_GUID;
	EFI_SHELL_PARAMETERS_PROTOCOL *EfiShellParametersProtocol = NULL;

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
	else {
		Print(L"StartImage Status = %x\n", status);
		FreePool(EfiShellParametersProtocol);
		return status;
	}

	FreePool(EfiShellParametersProtocol);
	return EFI_SUCCESS;
}

static EFI_STATUS
append_mok(UINTN derbufsize, void *derbuf)
{

	EFI_STATUS status;
	EFI_GUID VariableMoksbGuid = MOK_GUID;
	EFI_SIGNATURE_LIST *CertList;
	EFI_SIGNATURE_DATA *CertData;
	UINTN mokbuffersize;
	void *mokbuffer = NULL;
	UINT32 VariableAttr;

	EFI_GUID X509_GUID = { 0xa5c059a1, 0x94e4, 0x4aa7, {0x87, 0xb5, 0xab, 0x15, 0x5c, 0x2b, 0xf0, 0x72} };

	mokbuffersize = derbufsize + sizeof(EFI_SIGNATURE_LIST) + sizeof(EFI_GUID);
	mokbuffer = AllocatePool(mokbuffersize);

	if (!mokbuffer) {
		Print(L"Cannot AllocatePool.\n");
		uefi_call_wrapper(BS->Stall, 1, 1000000);
		status = EFI_OUT_OF_RESOURCES;
		goto out;

	}

	CertList = mokbuffer;
	CertList->SignatureType = X509_GUID;
	CertList->SignatureSize = 16 + derbufsize;

	CopyMem(mokbuffer + sizeof(EFI_SIGNATURE_LIST) + 16, derbuf,
	       derbufsize);

	CertData = (EFI_SIGNATURE_DATA *) (((UINT8 *) mokbuffer) +
					   sizeof(EFI_SIGNATURE_LIST));

	CertList->SignatureListSize = mokbuffersize;
	CertList->SignatureHeaderSize = 0;
	CertData->SignatureOwner = VariableMoksbGuid;

	VariableAttr = (EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_APPEND_WRITE);

	status = uefi_call_wrapper(RT->SetVariable, 5,
		L"MokList",
		&VariableMoksbGuid,
		VariableAttr,
		mokbuffersize,
		mokbuffer);

	if (status != EFI_SUCCESS) {
		Print(L"Enroll Key variable error.\n");
	}
	else {
		Print(L"Enroll key done.\n");
	}

	FreePool(mokbuffer);
	mokbuffer = NULL;

out:
	return status;
}


EFI_STATUS
EFIAPI
efi_main (EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
	UINT32 VariableAttr;
	EFI_STATUS status;

	EFI_GUID MokKeyEnrollGuid = MOKKEY_DER_ENROLL_GUID;
	EFI_GUID MokKeyTestKerGuid = MOKKEY_TEST_KERNEL_GUID;
	EFI_GUID MokKeySBEnableGuid = MOKKEY_SB_ENABLE_GUID;
	UINTN derbufsize = 1024;
	void *derbuf = NULL;
	BOOLEAN key_der_found = FALSE;
	BOOLEAN key_testker_found = FALSE;
	BOOLEAN key_sbenable_found = FALSE;

	VariableAttr = (EFI_VARIABLE_NON_VOLATILE|EFI_VARIABLE_BOOTSERVICE_ACCESS|EFI_VARIABLE_RUNTIME_ACCESS);

	InitializeLib(ImageHandle, SystemTable);

	derbuf = AllocatePool(derbufsize);
	if (!derbuf) {
		Print(L"Cannot AllocatePool.\n");
		return EFI_SUCCESS;
	}

	/* Mok Der key enroll */
	status = uefi_call_wrapper(RT->GetVariable, 5,
		L"MokKeyEnroll",
		&MokKeyEnrollGuid,
		&VariableAttr,   
		&derbufsize,
		derbuf);

	if (status != EFI_SUCCESS) {
		if (status == EFI_NOT_FOUND) {
			key_der_found = FALSE;
		}
		else if (status == EFI_BUFFER_TOO_SMALL) {
			FreePool(derbuf);
			derbuf = AllocatePool(derbufsize);
			if (!derbuf) {
				Print(L"Cannot AllocatePool.\n");
				uefi_call_wrapper(BS->Stall, 1, 10000000);
				return EFI_SUCCESS;
			}
			status = uefi_call_wrapper(RT->GetVariable, 5,
				L"MokKeyEnroll",
				&MokKeyEnrollGuid,
				&VariableAttr,
				&derbufsize,
				derbuf);
			if (status != EFI_SUCCESS) {
				Print(L"Get Der from variable error.\n");
				key_der_found = FALSE;
			} else
				key_testker_found = TRUE;
		}
		else {
			Print(L"Get Der from variable error.\n");
			key_der_found = FALSE;
		}
	} else {
		key_der_found = TRUE;
	}

	if (key_der_found) {
		Print(L"Append Mok Der key to mok.\n");
		status = append_mok(derbufsize, derbuf);
		if (status != EFI_SUCCESS) {
			Print(L"Append Mok Der key to mok error.\n");
			goto out;
		}
	}

	/* test kernel key enroll */
	status = uefi_call_wrapper(RT->GetVariable, 5,
		L"MokKeyTestKer",
		&MokKeyTestKerGuid,
		&VariableAttr,   
		&derbufsize,
		derbuf);

	if (status != EFI_SUCCESS) {
		if (status == EFI_NOT_FOUND) {
			key_testker_found = FALSE;
		} else if (status == EFI_BUFFER_TOO_SMALL) {
			FreePool(derbuf);
			derbuf = AllocatePool(derbufsize);
			if (!derbuf) {
				Print(L"Cannot AllocatePool.\n");
				uefi_call_wrapper(BS->Stall, 1, 10000000);
				return EFI_SUCCESS;
			}
			status = uefi_call_wrapper(RT->GetVariable, 5,
				L"MokKeyTestKer",
				&MokKeyEnrollGuid,
				&VariableAttr,
				&derbufsize,
				derbuf);
			if (status != EFI_SUCCESS) {
				Print(L"Get Der from variable error.\n");
				key_testker_found = FALSE;
			} else
				key_testker_found = TRUE;
		} else {
			Print(L"Get Der from variable error.\n");
			key_testker_found = FALSE;
		}
	} else {
		key_testker_found = TRUE;
	}

	if (key_testker_found) {
		Print(L"Append test kernel key to mok.\n");
		status = append_mok(derbufsize, derbuf);
		if (status != EFI_SUCCESS) {
			Print(L"Append test kernel key to mok error.\n");
			goto out;
		}
	}

	/* check SB enable variable */
	status = uefi_call_wrapper(RT->GetVariable, 5,
		L"MokSBEnable",
		&MokKeySBEnableGuid,
		&VariableAttr,   
		&derbufsize,
		derbuf);

	if (status != EFI_SUCCESS) {
		if (status == EFI_NOT_FOUND) {
			key_sbenable_found = FALSE;
		} else if (status == EFI_BUFFER_TOO_SMALL) {
			FreePool(derbuf);
			derbuf = AllocatePool(derbufsize);
			if (!derbuf) {
				Print(L"Cannot AllocatePool.\n");
				uefi_call_wrapper(BS->Stall, 1, 10000000);
				return EFI_SUCCESS;
			}
			status = uefi_call_wrapper(RT->GetVariable, 5,
				L"MokSBEnable",
				&MokKeySBEnableGuid,
				&VariableAttr,
				&derbufsize,
				derbuf);
			if (status != EFI_SUCCESS) {
				Print(L"Get SB sb state from variable error.\n");
				key_sbenable_found = FALSE;
			} else
				key_sbenable_found = TRUE;
		} else {
			Print(L"Get SB state from variable error.\n");
			key_sbenable_found = FALSE;
		}
	} else {
		key_sbenable_found = TRUE;
	}

	/* load and run the UbuntuSecBoot tool for enable secureboot */
	if (key_sbenable_found) {
		Print(L"load and run the UbuntuSecBoot tool for enable secureboot.\n");
		status = sb_enable(ImageHandle);
		if (status != EFI_SUCCESS) {
			Print(L"Enable SB by UbuntuSecBoot.efi error.\n");
		}
		else {
			Print(L"Enable SB done.\n");
		}
	}

out:

	VariableAttr = (EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS);

	/* delete the mok der variable */
	if (key_der_found) {
		status = uefi_call_wrapper(RT->SetVariable, 5,
			L"MokKeyEnroll",
			&MokKeyEnrollGuid,
			VariableAttr,   
			0,
			derbuf);

		if (status != EFI_SUCCESS)
			Print(L"Delete MokKeyEnroll variable error.\n");
		else
			Print(L"Delete MokKeyEnroll variable done.\n");
	}

	/* delete the var of test kernel */
	if (key_testker_found) {
		status = uefi_call_wrapper(RT->SetVariable, 5,
			L"MokKeyTestKer",
			&MokKeyTestKerGuid,
			VariableAttr,   
			0,
			derbuf);

		if (status != EFI_SUCCESS)
			Print(L"Delete MokKeyTestKer variable error.\n");
		else
			Print(L"Delete MokKeyTestKer variable done.\n");
	}

	/* delete the var of SB enable */
	if (key_testker_found) {
		status = uefi_call_wrapper(RT->SetVariable, 5,
			L"MokSBEnable",
			&MokKeySBEnableGuid,
			VariableAttr,   
			0,
			derbuf);

		if (status != EFI_SUCCESS)
			Print(L"Delete MokKeyTestKer variable error.\n");
		else
			Print(L"Delete MokKeyTestKer variable done.\n");
	}

	FreePool(derbuf);
	derbuf = NULL;

	status = delete_boot_entry();
	if (status != EFI_SUCCESS)
		Print(L"Delete boot entry fail.\n");
	else
		Print(L"Delete boot entrydone done.\n");

	uefi_call_wrapper(BS->Stall, 1, 10000000);

	uefi_call_wrapper(RT->ResetSystem, 4,
		EfiResetWarm,
		EFI_SUCCESS,
		0,
		NULL);

	return EFI_SUCCESS;
}
