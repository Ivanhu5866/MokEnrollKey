#ifndef AUTHVARDEF_H
#define AUTHVARDEF_H

#pragma pack(1)

typedef struct {
	EFI_GUID SignatureOwner;
	UINT8 SignatureData[1];
} EFI_SIGNATURE_DATA;

typedef struct {
	EFI_GUID SignatureType;
	UINT32 SignatureListSize;
	UINT32 SignatureHeaderSize;
	UINT32 SignatureSize;
	//UINT8 SignatureHeader[SignatureHeaderSize];
	//EFI_SIGNATURE_DATA Signatures[][SignatureSize];
} EFI_SIGNATURE_LIST;

#pragma pack()

#endif /* AUTHVARDEF_H */
