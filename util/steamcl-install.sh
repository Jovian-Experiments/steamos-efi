#!/bin/bash

set -eu;
shopt -s extglob;

. /etc/os-release

export LC_ALL=C;
ESP_UUID=c12a7328-f81f-11d2-ba4b-00a0c93ec93b;
esp_uuid=;  # uuid (partition type)
esp_dev=;   # /dev/sd?
esp_part=;  # X (as in /dev/sdaX)
esp_mount=; # usually /esp on SteamOS 3+
esp_fs=;    # fs type (vfat)

boot_entries=; # space separataed list of HEXBOOTID:LOADERPATH
all_boot_ids=; # space separataed list of HEXBOOTID
free_boot_id=; # an unused HEXBOOTID
boot_path_found=; # set if a matching boot entry was found by check_boot_path

distrib=${ID:-steamos};
distrib=${distrib,,};
manifest_checksum=;
manifest_version=;
checksum=;
installpath=EFI/$distrib;

datadir=;
libexecdir=;

warn () { echo "$@" >&2; }

############################################################################
# read in a packaging version file (checksum + package-version)
read_manifest ()
{
    manifest_checksum=;
    manifest_version=;
    src=$1;

    if [ -f $src ];
    then
        read manifest_checksum manifest_version < "$src";
    fi;

    return 0;
}

calculate_checksum ()
{
    local x=;
    local file=${1:-/dev/null};

    if [ -f"$file" ]
    then
        read checksum x < <(sha256sum "$file");
    fi;

    return 0;
}

############################################################################
# determine the location of the esp and its current mount point if any
find_esp ()
{
    esp_fs=;
    esp_dev=;
    esp_part=;
    esp_mount=;
    esp_uuid=;

    local part=;
    local dev=;
    local ptuid=;
    local fs=;
    local mount=;

    while read part dev ptuid fs mount;
    do
        if [ "$fs" != vfat ]; then continue; fi;

        if [ "$mount" = /esp ];
        then
            esp_fs="$fs";
            esp_dev="$dev";
            esp_part="$part";
            esp_mount="$mount";
            esp_uuid="$ptuid";
            break;
        fi;
    done < <(lsblk -nlp -o NAME,PKNAME,PARTTYPE,FSTYPE,MOUNTPOINT);

    # doesn't look like the ESP, try again:
    if [ "$ESP_UUID" != "$esp_uuid" ];
    then
        while read part dev ptuid fs mount;
        do
            if [ "$fs"    != vfat        ]; then continue; fi;
            if [ "$ptuid" != "$ESP_UUID" ]; then continue; fi;

            esp_fs="$fs";
            esp_dev="$dev";
            esp_part="$part";
            esp_mount="$mount";
            esp_uuid="$ptuid";
            break;
        done < <(lsblk -nlp -o NAME,PKNAME,PARTTYPE,FSTYPE,MOUNTPOINT);
    fi;

    if [ -z "$esp_dev" ] || [ -z "$esp_part" ];
    then
        warn "ESP not found by part-type ($ESP_UUID) or path (/esp)";
        return 1;
    fi;

    if [ -z "$mount" ];
    then
        warn "ESP on $esp_part is not mounted";
        return 1;
    fi;

    esp_part=${esp_part:${#esp_dev}};

    return 0;
}

############################################################################
# find matching efiboot entries:
find_boot_entries ()
{
    local distributor=${1:-debian}; shift;
    local dev=$1;                   shift;
    local part=${1:-999};           shift;
    local bootid=;
    local entry=;
    local label=;
    local epart=;
    local epath=;
    local edist=;

    all_boot_ids=;
    boot_entries=;
    distributor=${distributor,,};

    while read -r bootid entry;
    do

        bootid=${bootid%\*};

        case $bootid in (Boot+([0-9])) true; ;; *) continue; esac;

        all_boot_ids=${all_boot_ids}${all_boot_ids:+ }${bootid#Boot};

        case $entry  in (*+([ 	])HD\(*) true; ;; *) continue; esac;

        label=${entry%%HD\(*};

        epart=${entry#*HD\(};
        epart=${epart%%,*}

        if [ ! ${epart:-0} -eq ${part} ]; then continue; fi;

        epath=${entry##*/File\(};
        epath=${epath%\)*};
        epath=${epath//\\/\/};

        edist=${epath#?(/)EFI/}
        edist=${edist%%/*};

        if [ "${edist,,}" = "$distributor" ];
        then
            local new_entry=${bootid#Boot}:$epath;
            boot_entries=$boot_entries${boot_entries:+ }${new_entry};
        fi;
    done < <(efibootmgr -v);
}

choose_free_boot_id ()
{
    free_boot_id=;
    local id=0;

    for be in "$@";
    do
        if [ $((16#$be)) = $id ]; then : $((id++)); fi;
    done

    free_boot_id=$(printf "%x" $id);
}

update_esp ()
{
    local dst=$1; shift;
    local esp=$1; shift;
    local bin=$1; shift;
    local lst=$1; shift;
    local entry=;

    for entry in "$@";
    do
        local bootnum=${entry%%:*};
        local bootpath=${entry#*:};

        if [ "${bootpath,,}" = /efi/"$dst"/steamcl.efi ]
        then
            echo Replacing boot entry Boot$bootnum \@ $bootpath;
            cp -av $bin $lst $esp${bootpath%/*};
            return 0;
        fi;
    done;

    return 1;
}

add_boot_entry ()
{
    local dst=$1;
    local esp=$2;
    local bin=$3;
    local lst=$4;
    local bid=$5;
    local dev=$6;
    local gpt=$7;
    local nam=$8;

    local dir=/EFI/"$dst";
    local path="$esp""$dir";
    local ldr=$(basename $bin);

    printf "Creating Boot%04x %s\n" ${bid:-0} "$nam";
    dir=${dir//\//\\};
    efibootmgr -b $bid -c -d $dev -p $gpt -L "$nam" -l "$dir\\$ldr" > /dev/null;
}

check_boot_path ()
{
    local part=$1;
    local path=${2,,};
    boot_path_found=;

    while read -r bootid entry;
    do
        bootid=${bootid%\*};
        case $bootid in (Boot+([0-9]))   true; ;; *) continue; esac;
        case $entry  in (*+([ 	])HD\(*) true; ;; *) continue; esac;

        epart=${entry#*HD\(};
        epart=${epart%%,*}

        if [ ! ${epart:-0} -eq ${part} ]; then continue; fi;

        epath=${entry##*/File\(};
        epath=${epath%\)*};
        epath=${epath//\\/\/};
        epath=${epath,,};

        #echo "CMP $epath vs /$path";

        if [ "$epath" = "/$path" ];
        then
            boot_path_found=$epath;
            break;
        fi;
    done < <(efibootmgr -v);

    [ "$boot_path_found" != "" ];
}

############################################################################

find_esp;
echo "ESP $esp_fs $esp_uuid on $esp_dev GPT#$esp_part ($esp_mount)";
find_boot_entries $distrib $esp_dev $esp_part;

# now some diagnotics:
esp_csum_ok=0;  # does the binary's checksum match the manifest's?
esp_boot_ok=0   # is there a bootloader entry for the canonical loader path?
pkg_csum_ok=0;  # is the package's bootloader binary ok?
esp_version=;   # version of the loader on the ESP (according to manifest)
pkg_version=;   # version of the loader package in the OS
pkg_checksum=;  # checksum of the loader binary in the OS package
pkg_is_newer=0; # OS package has a newer loader then the ESP
install_cl=0;

read_manifest "$esp_mount/$installpath"/steamcl.version;
calculate_checksum "$esp_mount/$installpath"/steamcl.efi;
if [ ${checksum:-0} = ${manifest_checksum:-1} ];
then
    esp_csum_ok=1;
    esp_version=$manifest_version;
fi;

if check_boot_path $esp_part $installpath/steamcl.efi;
then
    esp_boot_ok=1;
fi;

read_manifest "$datadir"/steamcl.version;
calculate_checksum "$libexecdir"/steamcl.efi;
if [ ${checksum:-0} = ${manifest_checksum:-1} ];
then
    pkg_csum_ok=1;
    pkg_version=$manifest_version;
    pkg_checksum=$manifest_checksum;
fi;

if dpkg --compare-versions "${esp_version:-0}" lt "${pkg_version:-0}";
then
    pkg_is_newer=1;
fi

# let's walk through the logic here:
# bootloader entry !ok → create bootloader entry
# if package checksum is ok
# and esp is damaged/absent/old → install new bootloader binary + manifest

install_ok=1 # might not want/need a copy at all

if [ ${pkg_is_newer:-0} = 1 ] || [ $esp_csum_ok = 0 ];
then
    install_ok=0; # ok, we do need to copy the files in:
    if [ $pkg_csum_ok = 1 ]
    then
        if update_esp $distrib $esp_mount \
                      $libexecdir/steamcl.efi $datadir/steamcl.version \
                      $boot_entries;
        then
            install_ok=1;
        fi;
    else
        echo Package checksum failure: $libexecdir/steamcl.efi:;
        echo " " checksum: $checksum;
        echo " " expected: $pkg_checksum;
    fi;
fi;

if [ $esp_boot_ok = 0 ];
then
    choose_free_boot_id $all_boot_ids;
    if add_boot_entry $distrib $esp_mount \
                      $libexecdir/steamcl.efi $datadir/steamcl.version \
                      $free_boot_id $esp_dev $esp_part "SteamOS/Clockwerk";
    then
        esp_boot_ok=1;
    fi;
fi;

if [ $install_ok = 0 ];
then
    echo ESP: Failed to install $libexecdir/steamcl.efi on $esp_mount;
    exit 1;
fi;

if [ $esp_boot_ok = 0 ];
then
    echo EFI: Failed to create boot entry $free_boot_id;
    exit 1;
fi;

exit 0;
