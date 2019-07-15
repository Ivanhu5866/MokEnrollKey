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

#define GNVN_BUF_SIZE 1024

EFI_GUID empty_guid = {0x0,0x0,0x0,{0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0}};
EFI_GUID global_variable_guid = EFI_GLOBAL_VARIABLE;

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
EFIAPI
efi_main (EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
	UINT32 VariableAttr;
	EFI_GUID VariableMoksbGuid = MOK_GUID;
	EFI_STATUS status;

	EFI_GUID MokKeyEnrollGuid = MOKKEY_DER_ENROLL_GUID;
	UINTN derbufsize = 1024;
	void *derbuf = NULL;

	EFI_SIGNATURE_LIST *CertList;
	EFI_SIGNATURE_DATA *CertData;
	UINTN mokbuffersize;
	void *mokbuffer = NULL;
	EFI_GUID X509_GUID = { 0xa5c059a1, 0x94e4, 0x4aa7, {0x87, 0xb5, 0xab, 0x15, 0x5c, 0x2b, 0xf0, 0x72} };

	VariableAttr = (EFI_VARIABLE_NON_VOLATILE|EFI_VARIABLE_BOOTSERVICE_ACCESS|EFI_VARIABLE_RUNTIME_ACCESS);

	InitializeLib(ImageHandle, SystemTable);

	derbuf = AllocatePool(derbufsize);
	if (!derbuf) {
		Print(L"Cannot AllocatePool.\n");
		return EFI_SUCCESS;
	}

	status = uefi_call_wrapper(RT->GetVariable, 5,
		L"MokKeyEnroll",
		&MokKeyEnrollGuid,
		&VariableAttr,   
		&derbufsize,
		derbuf);

	if (status != EFI_SUCCESS) {
		if (status == EFI_BUFFER_TOO_SMALL) {
			FreePool(derbuf);
			derbuf = AllocatePool(derbufsize);
			if (!derbuf) {
				Print(L"Cannot AllocatePool.\n");
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
				goto out;
			}
		}
		else {
			Print(L"Get Der from variable error.\n");
			goto out;
		}
	}

	mokbuffersize = derbufsize + sizeof(EFI_SIGNATURE_LIST) + sizeof(EFI_GUID);
	mokbuffer = AllocatePool(mokbuffersize);

	if (!mokbuffer) {
		Print(L"Cannot AllocatePool.\n");
		uefi_call_wrapper(BS->Stall, 1, 1000000);
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

	VariableAttr = (EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS);

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
