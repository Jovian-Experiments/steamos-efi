#pragma once

EFI_STATUS efi_file_exists (IN EFI_FILE_PROTOCOL *dir, CONST IN CHAR16 *path);

EFI_STATUS efi_file_open (IN EFI_FILE_PROTOCOL *dir,
                          OUT EFI_FILE_PROTOCOL **opened,
                          CONST IN CHAR16 *path,
                          UINT64 mode,
                          UINT64 attr);

EFI_STATUS efi_file_close (IN EFI_FILE_PROTOCOL *file);

EFI_STATUS efi_readdir (IN EFI_FILE_PROTOCOL *dir,
                        IN OUT EFI_FILE_INFO **dirent,
                        IN OUT UINTN *dirent_size);

EFI_STATUS efi_file_read (IN EFI_FILE_PROTOCOL *fh,
                          IN OUT CHAR8 *buf,
                          IN OUT UINTN *bytes);

EFI_STATUS efi_mount (IN EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *part,
                      OUT EFI_FILE_PROTOCOL **root);

EFI_STATUS efi_unmount (IN OUT EFI_FILE_PROTOCOL **root);

VOID ls (EFI_FILE_PROTOCOL *dir,
         UINTN indent,
         CONST CHAR16 *name,
         UINTN recurse);

EFI_DEVICE_PATH * make_device_path (IN EFI_HANDLE device, IN CHAR16 *path);

EFI_DEVICE_PATH * append_path_to_device_path (IN EFI_DEVICE_PATH *dpath, IN CHAR16 *path);
