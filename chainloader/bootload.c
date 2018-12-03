#include <efi.h>
#include <efilib.h>
#include <efiprot.h>

#include "err.h"
#include "util.h"
#include "fileio.h"
#include "bootload.h"

// this is x86_64 specific
#define EFI_STUB_ARCH 0x8664

EFI_STATUS valid_efi_binary (IN EFI_FILE_PROTOCOL *dir, CONST IN CHAR16 *path)
{
    EFI_STATUS res;
    EFI_FILE_PROTOCOL *bin = NULL;
    CHAR8 header[512] = { '0','x','d','e','a','d','b','e','e','f', 0, 0 };
    CONST UINTN hsize = sizeof(header);
    UINTN bytes = hsize;
    UINTN s;
    UINT16 arch;

    res = efi_file_open( dir, &bin, path, 0, 0 );
    ERROR_RETURN( res, res, L"open( %s )", path );

    res = efi_file_read( bin, (CHAR8 *)header, &bytes );
    ERROR_RETURN( res, res, L"read( %s, %u )", path, hsize );

    efi_file_close( bin );

    if( bytes < hsize )
        return EFI_END_OF_FILE;

    if( header[0] != 'M' || header[1] != 'Z' )
        return EFI_LOAD_ERROR;

    // The uint32 starting at offset 0x3c
    s = * (UINT32 *) &header[ 0x3c ];

    if( s >=  0x180 ||
        header[ s   ] != 'P' ||
        header[ s+1 ] != 'E' ||
        header[ s+2 ] != 0   ||
        header[ s+3 ] != 0   )
        return EFI_LOAD_ERROR;

    arch = * (UINT16 *) &header[ s+4 ];

    if( arch != EFI_STUB_ARCH )
        return EFI_LOAD_ERROR;

    return EFI_SUCCESS;
}

EFI_STATUS choose_steamos_loader (IN EFI_HANDLE *handles,
                                  CONST IN UINTN n_handles,
                                  IN OUT bootloader *chosen)
{
    EFI_STATUS res;
    EFI_FILE_PROTOCOL *root_dir = NULL;
    static EFI_GUID fs_guid = SIMPLE_FILE_SYSTEM_PROTOCOL;
    static EFI_GUID dp_guid = DEVICE_PATH_PROTOCOL;

    chosen->partition = NULL;
    chosen->loader_path = NULL;

    for ( UINTN i = 0; i < n_handles; i++ )
    {
        EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs = NULL;

        efi_unmount( &root_dir );

        res = get_handle_protocol( &handles[i], &fs_guid, (VOID **)&fs );
        ERROR_CONTINUE( res, L"handle #%u: no simple file system protocol", i );

        res = efi_mount( fs, &root_dir );
        ERROR_CONTINUE( res, L"partition #%u not opened", i );

        res = efi_file_exists( root_dir, BOOTCONFPATH );
        if( res != EFI_SUCCESS )
            continue;

        // TODO: parse the config file here to pick the bootloader
        // TODO: If we've already seen a candidate, compare the
        // new bootconfig with the old one to see if it's a better choice
        // for now we're just picking the first option because we want to
        // get the actual bootloader exec step working

        res = efi_file_exists( root_dir, STEAMOSLDR );
        if( res != EFI_SUCCESS )
            continue;

        res = valid_efi_binary( root_dir, STEAMOSLDR );
        ERROR_CONTINUE( res, L"%s is not an EFI executable", STEAMOSLDR );

        res = get_handle_protocol( &handles[i], &dp_guid,
                                   (VOID **) &chosen->device_path );
        ERROR_CONTINUE( res, L"Unable to get device path for partition #%u", 1 );

        chosen->partition = handles[i];
        chosen->loader_path = STEAMOSLDR;
        break;
    }

    efi_unmount( &root_dir );

    return chosen->partition ? EFI_SUCCESS : EFI_NOT_FOUND;
}

static VOID dump_bootloader_paths (EFI_HANDLE *current, EFI_DEVICE_PATH *target)
{
    CHAR16 *this = NULL;
    CHAR16 *that = NULL;
    EFI_GUID lip_guid = LOADED_IMAGE_PROTOCOL;
    EFI_LOADED_IMAGE *li;
    EFI_STATUS res;
    EFI_DEVICE_PATH *fqdp = NULL;

    that = DevicePathToStr( target );
    Print( L"Loading bootloader @ %s\n", that );

    res = get_handle_protocol( current, &lip_guid, (VOID **) &li );
    ERROR_RETURN( res, , L"No loaded image protocol. wat." );

    fqdp = AppendDevicePath( DevicePathFromHandle( li->DeviceHandle ),
                             li->FilePath );

    this = DevicePathToStr( fqdp );

    Print( L"Within chainloader @ %s\n", this );
}

static const CHAR16 *memtype (EFI_MEMORY_TYPE m)
{
    switch (m)
    {
      case EfiReservedMemoryType:      return L"Reserved";
      case EfiLoaderCode:              return L"Loader Code";
      case EfiLoaderData:              return L"Loader Data";
      case EfiBootServicesCode:        return L"Boot Services Code";
      case EfiBootServicesData:        return L"Boot Services Data";
      case EfiRuntimeServicesCode:     return L"Runtime Services Code";
      case EfiRuntimeServicesData:     return L"Runtime Services Data";
      case EfiConventionalMemory:      return L"Conventional Memory";
      case EfiUnusableMemory:          return L"Unusable Memory";
      case EfiACPIReclaimMemory:       return L"ACPI Reclaim Memory";
      case EfiACPIMemoryNVS:           return L"ACPI Memory NVS";
      case EfiMemoryMappedIO:          return L"Memory Mapped IO";
      case EfiMemoryMappedIOPortSpace: return L"Memory Mapped IO Port Space";
      case EfiPalCode:                 return L"Pal Code";
      case EfiMaxMemoryType:           return L"(INVALID)";
      default:
        return L"???";
    }
}

EFI_STATUS exec_bootloader (EFI_HANDLE *current_image, bootloader *boot)
{
    EFI_STATUS res = EFI_SUCCESS;
    EFI_HANDLE efi_app = NULL;
    EFI_GUID load_guid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
    EFI_LOADED_IMAGE *child = NULL;
    EFI_DEVICE_PATH *dpath = NULL;
    //EFI_DEVICE_PATH *dp2 = NULL;
    UINTN esize;
    CHAR16 *edata = NULL;

    dpath = FileDevicePath( boot->partition, boot->loader_path );
    //dp2 = make_device_path( boot->partition, boot->loader_path );
    if( !dpath )
        res = EFI_INVALID_PARAMETER;

    ERROR_JUMP( res, unload,
                L"FDP could not construct a device path from %x + %s",
                (UINT64) &boot->device_path, boot->loader_path );

    dump_bootloader_paths( current_image, dpath );

    res = uefi_call_wrapper(BS->LoadImage, 6, FALSE,
                            *current_image, dpath, NULL, 0, &efi_app);

    ERROR_JUMP( res, unload, L"load-image failed" );

    // TODO: do the self-reload trick to keep shim + EFI happy
    // we don't can't support secureboot yet because of the NVIDIA
    // module/dkms/initrd problem, but if we ever fix that, we'll
    // need to do what refind.main.c@394 does.

    res = get_handle_protocol( &efi_app, &load_guid, (VOID **) &child );
    ERROR_JUMP( res, unload, L"loaded-image-protocol not found" );

    Print( L"Zeroing out command line options to %x\n--\n", (UINT64) child );
    child->LoadOptions = L"";
    child->LoadOptionsSize = 0;

    Print( L"\n\
typedef struct {                                               \n\
    UINT32                          Revision;         %u       \n\
    EFI_HANDLE                      ParentHandle;     %x (%x)  \n\
    struct _EFI_SYSTEM_TABLE        *SystemTable;     %x       \n\
                                                               \n\
    // Source location of image                                \n\
    EFI_HANDLE                      DeviceHandle;     %x       \n\
    EFI_DEVICE_PATH                 *FilePath;        %s       \n\
    VOID                            *Reserved;        %x       \n\
                                                               \n\
    // Images load options                                     \n\
    UINT32                          LoadOptionsSize;  %u       \n\
    VOID                            *LoadOptions;   \"%s\"     \n\
                                                               \n\
    // Location of where image was loaded                      \n\
    VOID                            *ImageBase;       %x       \n\
    UINT64                          ImageSize;        %lu      \n\
    EFI_MEMORY_TYPE                 ImageCodeType;    %s       \n\
    EFI_MEMORY_TYPE                 ImageDataType;    %s       \n\
                                                               \n\
    // If the driver image supports a dynamic unload request   \n\
    EFI_IMAGE_UNLOAD                Unload;           %x       \n\
} EFI_LOADED_IMAGE_PROTOCOL;                                   \n",
           child->Revision,
           child->ParentHandle, *current_image,
           (UINT64) child->SystemTable,
           child->DeviceHandle,
           DevicePathToStr( child->FilePath ),
           child->Reserved,
           child->LoadOptionsSize,
           (CHAR16 *)child->LoadOptions,
           (UINT64)child->ImageBase,
           child->ImageSize,
           memtype( child->ImageCodeType ),
           memtype( child->ImageDataType ),
           (UINT64) child->Unload );
    res = uefi_call_wrapper( BS->StartImage, 3, efi_app, &esize, &edata );
    WARN_STATUS( res, L"start image returned with exit code: %u; data @ 0x%x",
                 esize, (UINT64) edata );

unload:
    if( efi_app )
    {
        EFI_STATUS r2 = uefi_call_wrapper( BS->UnloadImage, 1, efi_app );
        WARN_STATUS( r2, L"unload of image failed" );
    }

    efi_free( dpath );

    return res;
}

