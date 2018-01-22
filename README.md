# Matryoshka

```
make
sudo insmod dm-mks.ko mks_debug_mode=1
sudo rmmod dm_mks
```

### Targets

```
echo 0 1024 mks "passphrase" "/dev/sdXY" | dmsetup create matryoshka
```