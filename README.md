# Artifice

Artifice is a device mapper implementation for a deniable file system. It is able to hide critical files in a way which is deniable for the owner.

For contributors to this project, please look at the "Guidelines for development" section.

## Build

Linux Kernel Module
```
Compile: make
Inserting module: sudo insmod dm-mks.ko mks_debug_mode=1
Removing module: sudo rmmod dm_mks
```

Device Mapper target
```
echo 0 1024 mks passphrase "/dev/sdXY" | sudo dmsetup create matryoshka
```

## Guidelines for development

Artifice is best developed using a virtual machine. You may choose to develop directly on the virtual machine (not recommended) or you can develop on the host machine and use `scp` to transfer your code (recommended). 

**Do not push to git on the host machine and pull on the virtual machine to transfer files as this clogs the git commit log and makes it difficult to trace progress.**

The `Makefile` contains some helpful (totally hacky) targets which can speed up loading and creating device mapper targets.

```
make
make debug: Insert module and create a device mapper target.
make debug_end: Remove device mapper target and remove module.
```

The *debug* target expects a block device "/dev/sdb1" to exist in the system. The easiest way to have this in VirtualBox is to create an extra disk and make a GPT partition table on it. Then create a single partition and format it as FAT32.

```
$ sudo parted /dev/sdb
--> mklabel gpt
--> mkpart test fat32 0 -1

$ sudo mkfs.fat -F32 /dev/sdb1
```
