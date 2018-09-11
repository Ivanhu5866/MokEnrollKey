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

	uefi_call_wrapper(BS->Stall, 1, 10000000);

	uefi_call_wrapper(RT->ResetSystem, 4,
		EfiResetWarm,
		EFI_SUCCESS,
		0,
		NULL);

	return EFI_SUCCESS;
}
