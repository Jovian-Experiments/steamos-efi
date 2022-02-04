// steamos-efi  --  SteamOS EFI Chainloader

// SPDX-License-Identifier: GPL-2.0+
// Copyright © 2021 Collabora Ltd
// Copyright © 2021 Valve Corporation

// steamos-efi is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 2.0 of the License, or
// (at your option) any later version.

// steamos-efi is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License
// along with steamos-efi.  If not, see <http://www.gnu.org/licenses/>.

#include <efi.h>
#include <efilib.h>
#include <efiprot.h>
#include <eficon.h>

#include "err.h"
#include "util.h"
#include "console-ex.h"

static EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL *console;

static BOOLEAN
init_console_ex (void)
{
    EFI_STATUS res;
    EFI_GUID input_guid = EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL_GUID;

    if( !console )
    {
        res = get_handle_protocol( &ST->ConsoleInHandle, &input_guid,
                                   (VOID **)&console );
        ERROR_RETURN( res, FALSE, L"console-ex init failed" );
    }

    if( console )
        return TRUE;

    return FALSE;
}

EFI_HANDLE *
bind_key (UINT16 scan, CHAR16 chr, IN EFI_KEY_HANDLER handler)
{
    EFI_STATUS res;
    EFI_HANDLE *binding;
    EFI_KEY_DATA key = { { SCAN_NULL, CHAR_NULL }, { 0, 0 } };

    key.key.ScanCode = scan;
    key.key.UnicodeChar = chr;

    if( !init_console_ex() )
        return NULL;

    res = uefi_call_wrapper( console->register_key, 4, console,
                             &key, handler, (VOID **)&binding );

    ERROR_RETURN( res, NULL,
                  L"Cannot bind key {%u, 0x%04x} to callback\n", scan, chr );

    return binding;
}

EFI_STATUS
unbind_key (EFI_HANDLE *binding)
{
    if( !console )
        return EFI_NOT_READY;

    return uefi_call_wrapper( console->unregister_key, 2, console, binding );
}
