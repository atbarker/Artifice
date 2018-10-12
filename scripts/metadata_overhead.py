"""
Script used to calculate different overhead consumption
based on different configurations of a artifice file
system. 

Author: Yash Gupta <ygupta@ucsc.edu>
"""
import math

def roundup_to_p2(number):
    """
    Round up to nearest power of two using
    a simple check to see if a number is already
    a power of two or not.
    """

    if number & (number-1) == 0:
        return number
        
    number *= 2
    return 2 ** long(math.log(number, 2))

class afs_config:
    """
    Default Values:
    shards        = 4
    hash_bits     = 128 (16 Bytes)
    afs_bits      = 32  (4 GB instance)
    disk_bits     = 40  (1 TB storage)
    blk_bits      = 12  (4 KB block)
    """

    def __init__(self, shards=8, checksum_bits=0, hash_bits=128, afs_bits=32, disk_bits=40, blk_bits=12):
        self.shards = shards
        self.checksum_bits = checksum_bits
        self.hash_bits = hash_bits
        self.afs_bits = afs_bits
        self.disk_bits = disk_bits
        self.blk_bits = blk_bits

        ## Sizes in bytes due to bits
        self.checksum_sz = checksum_bits / 8
        self.hash_sz = hash_bits / 8
        self.blk_sz = 2 ** blk_bits
        self.afs_sz = 2 ** afs_bits
        self.disk_sz = 2 ** disk_bits

        ## Number of blocks (and bits required to represent them)
        ## for artifice instance and the disk.
        self.afs_blks = self.afs_sz / self.blk_sz
        self.afs_blks_bits = int(math.log(self.afs_blks, 2))

        self.disk_blks = self.disk_sz / self.blk_sz
        self.disk_blks_bits = int(math.log(self.disk_blks, 2))

        ## Sizes for the Artifice Map
        self.afs_map_diskblk_sz = roundup_to_p2(self.disk_blks_bits) / 8
        self.afs_map_entry_sz = (self.shards * self.checksum_sz) + (self.shards * self.afs_map_diskblk_sz) + self.hash_sz
        self.afs_map_sz = self.afs_blks * self.afs_map_entry_sz

        self.overhead = ((float(self.afs_map_sz) / float(self.afs_sz)) * 100) * self.shards

    def get_overhead(self):
        return self.overhead

default = afs_config()
lower_ss4 = afs_config(shards = 4)
lower_ss6 = afs_config(shards = 6)
bigger_afs5 = afs_config(afs_bits=33)
sha1 = afs_config(hash_bits = 160)
bigger_disk = afs_config(disk_bits = 43)

print "Default        :", str(default.get_overhead()), "%"
print "Less Shards (4):", str(lower_ss4.get_overhead()), "%"
print "Less Shards (6):", str(lower_ss6.get_overhead()), "%"
print "Bigger MKS (33):", str(bigger_afs5.get_overhead()), "%"
print "SHA1 (20 Bytes):", str(sha1.get_overhead()), "%"
print "Big Disk  (8TB):", str(bigger_disk.get_overhead()), "%"