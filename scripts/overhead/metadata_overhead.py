import numpy as np
import matplotlib.pyplot as plt

"""
Script used to calculate different overhead consumption
based on different configurations of a artifice file
system. 

Author: Yash Gupta <ygupta@ucsc.edu>, Austen Barker <atbarker@ucsc.edu>
"""

#in this program we see the following numbers
#tuple shards:
#shards:
#afs_bits: block offset bits (default is 32 bits per block ID)
#blk_bits: Size of each block (4KB, or 12 is standard)
#entropy size: size of the entropy block in bytes
#block checksum: size of the block checksum in bytes 16 bit checksum standard (cityhash)
#data hash size: 16 bytes standard (Sha128 bit)

class afs_config:
    def __init__(self, algorithm='SSS', tuple_shards=4, shards=4, afs_bits=32, blk_bits=12, entropy_sz=8, block_checksum=2, data_hash_sz=16, data_blocks=0):
        ## Entry in the map.
        tuple_sz = block_checksum + afs_bits/8
        #TODO if we are dealing with AONT or some sort of RS+entropy we need to go through and make sure entropy block pointers or key material are stored properly
        #Right now in the case of the AONT system our initialization vector is just the passphrase hash.
        if algorithm == "RS":
            tuple_sz = tuple_sz + afs_bits/8
        map_entry_sz = (tuple_shards * tuple_sz) + data_hash_sz + entropy_sz
        print "map_entry_sz: " + str(map_entry_sz) + " bytes"

        ## Entire map.
        ## if we do it this way we can just get a straight percentage of the metadata overhead
        num_blocks = 2 ** (afs_bits - blk_bits)
        map_sz = map_entry_sz * num_blocks
        map_sz *= shards

        ## Map Block overhead.
        map_entries_per_table = ((2 ** blk_bits)-64) / map_entry_sz
        num_map_tables = num_blocks / map_entries_per_table
        num_map_blocks = (num_map_tables - 975) / 1019
        map_blocks_sz = num_map_blocks * 4096
        map_blocks_sz *= shards
        print "map_blocks_sz: " + str(((float(map_blocks_sz) / 1024) / 1024)) + " MB"
        print

        ## Overhead.
        afs_sz = 2 ** afs_bits
        self.overhead = (float(map_sz + map_blocks_sz) / float(afs_sz)) * 100.0

    def get_overhead(self):
        return self.overhead

default = afs_config()
higher_shards = afs_config(shards = 8)
higher_tuple_shards = afs_config(tuple_shards=8)
high = afs_config(shards=8, tuple_shards=8)
sectors = afs_config(blk_bits=9)
entropy_filename = afs_config(entropy_sz=32)

print "Default        :", str(default.get_overhead()), "%"
print "High Shards (8):", str(higher_shards.get_overhead()), "%"
print "Tuple Shards(8):", str(higher_tuple_shards.get_overhead()), "%"
print "High        (8):", str(high.get_overhead()), "%"
#so I think this is one where we just do individual sectors
print "Sectors   (512):", str(sectors.get_overhead()), "%"
print "Entropy    (32):", str(entropy_filename.get_overhead()), "%"


m_values = np.arange(3, 10, 1)
prob1 = []
prob2 = []
prob3 = []
for i in m_values:
    #1 entropy block, 1 data block, i parity blocks
    #in this case at least i-2 blocks must survive, the threshold is tied to the
    #number of carrier blocks in the keyword
    sss = afs_config(shards=i, tuple_shards=i)
    aont = afs_config(algorithm='AONT', shards=i, tuple_shards=i)
    rs = afs_config(algorithm="RS", shards=i, tuple_shards=i)
    prob1.append(sss.get_overhead())
    prob2.append(aont.get_overhead())
    prob3.append(rs.get_overhead())

plt.xlabel("Number of Carrier Blocks")
plt.ylabel("Metadata Overhead (percentage)")
plt.title("Number of Carrier Blocks vs Metadata Overhead")
rs = plt.plot(m_values, prob3, label='RS', marker="o")
shamir = plt.plot(m_values, prob1, label='SSS', marker="D")
#ssms = plt.plot(m_values, prob5, label='SSMS, threshold 2', marker="x")
aont = plt.plot(m_values, prob2, label='AONT', marker="x")
plt.legend()
plt.show()
