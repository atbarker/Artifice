#!/usr/bin/python3
import numpy as np
import math
import sys
import matplotlib.pyplot as plt

block_size = 4096 #4KB
def_total_size = 549755813888 #512GB
def_free_blocks = def_total_size / block_size
def_overwritten = 1310720 #5GB in 4KB blocks
def_art_size = 262144 #1GB artifice instance
#def_art_size = 524288 #2GB artifice instance
#def_art_size = 1310720 #5GB artifice instance
prob_success = def_overwritten / def_free_blocks
num_days = 365
small_checksum = 0 #checksum used to verify carrier block integrity


def calc_metadata_size_rs(blocks, parity, entropy, data, replicas, verbose):
    pointer_size = 4
    art_block_hash = 16
    entropy_filename_hash = 8

    amplification_factor = parity / data
    carrier_block_tuple = pointer_size + small_checksum
    record_size = (parity * carrier_block_tuple) + (entropy * pointer_size) + art_block_hash + entropy_filename_hash
    pointers_per_pointerblock = (block_size / pointer_size - 1)

    entries_per_block = math.floor(block_size / record_size)
    num_map_blocks = math.ceil((blocks / data) / entries_per_block)
    num_map_map_blocks = math.ceil((num_map_blocks / data) / entries_per_block)
    num_pointer_blocks = math.ceil(num_map_map_blocks / pointers_per_pointerblock)
    metadata_size = ((num_pointer_blocks + 1) * replicas) + num_map_map_blocks + num_map_blocks
    metadata_overhead = (metadata_size / blocks) * 100
    effective_artifice_size = (blocks * amplification_factor) + metadata_size

    if verbose == True:
        print("|---Metadata size for Reed-Solomon-----|")
        print("Codeword configuration: {} data blocks, {} entropy, {} parity blocks".format(data, entropy, parity))
        print("Write amplification factor: {}".format(amplification_factor))
        print("Record Size: {} bytes".format(record_size))
        print("Entries per map block: {}".format(entries_per_block))
        print("Number of map blocks: {}".format(num_map_blocks))
        print("Number of map map blocks: {}".format(num_map_map_blocks))
        print("Number of pointer bocks: {}".format(num_pointer_blocks))
        print("Metadata size in blocks: {}".format(metadata_size))
        print("Metadata overhead: {} percent".format(metadata_overhead))
        print("effective artifice size: {}".format(effective_artifice_size))
        print("")

    return metadata_size

def calc_metadata_size_shamir(blocks, shares, threshold, replicas, verbose):
    pointer_size = 4
    art_block_hash = 16

    amplification_factor = shares
    carrier_block_tuple = pointer_size + small_checksum
    record_size = (shares * carrier_block_tuple) + art_block_hash
    pointers_per_pointerblock = (block_size / pointer_size) - 1

    entries_per_block = math.floor(block_size / record_size)
    num_map_blocks = math.ceil(blocks / entries_per_block)
    num_map_map_blocks = math.ceil(num_map_blocks / entries_per_block)
    num_pointer_blocks = math.ceil(num_map_map_blocks / pointers_per_pointerblock)
    metadata_size = ((num_pointer_blocks + 1) * replicas) + num_map_map_blocks + num_map_blocks
    metadata_overhead = (metadata_size / blocks) * 100
    effective_artifice_size = (blocks * amplification_factor) + metadata_size
   
    if verbose == True:
        print("|---Metadata size for Secret Sharing-----|")
        print("Codeword configuration: reconstruct threshold {}, {} shares".format(threshold, shares))
        print("Write amplification factor: {}".format(shares))
        print("Record Size: {} bytes".format(record_size))
        print("Entries per map block: {}".format(entries_per_block))
        print("Number of map blocks: {}".format(num_map_blocks))
        print("Number of map map blocks: {}".format(num_map_map_blocks))
        print("Number of pointer bocks: {}".format(num_pointer_blocks))
        print("Metadata size in blocks: {}".format(metadata_size))
        print("Metadata overhead: {} percent".format(metadata_overhead))
        print("effective artifice size: {}".format(effective_artifice_size))
        print("")

    return metadata_size

def calc_metadata_size_ssms(blocks, shares, threshold, replicas, verbose):
    pointer_size = 4
    art_block_hash = 16

    #So we will only have to store records for each encoding "group"
    #So say we have 8 shards per block, the data blocks for all those 8 shards can be represented by one map entry
    #store datablocks/threshold entries, determine the entry for a data block with data block # / threshold
    #essentiall index the same as a bitvector
    amplification_factor = (shares + threshold)/threshold
    carrier_block_tuple = pointer_size + small_checksum
    record_size = (shares * carrier_block_tuple) + art_block_hash
    pointers_per_pointerblock = (block_size / pointer_size) - 1

    entries_per_block = math.floor(block_size / record_size)
    num_map_blocks = math.ceil(math.ceil(blocks / entries_per_block)/threshold)
    num_map_map_blocks = math.ceil(math.ceil(num_map_blocks / entries_per_block)/threshold)
    num_pointer_blocks = math.ceil(num_map_map_blocks / pointers_per_pointerblock)
    metadata_size = ((num_pointer_blocks + 1) * replicas) + num_map_map_blocks + num_map_blocks
    metadata_overhead = (metadata_size / blocks) * 100
    effective_artifice_size = (blocks * amplification_factor) + metadata_size

    if verbose == True:
        print("|---Metadata size for Secret Sharing Made Short-----|")
        print("Codeword configuration: reconstruct threshold {}, {} shares".format(threshold, shares))
        print("Write amplification factor: {}".format(shares))
        print("Record Size: {} bytes".format(record_size))
        print("Entries per map block: {}".format(entries_per_block))
        print("Number of map blocks: {}".format(num_map_blocks))
        print("Number of map map blocks: {}".format(num_map_map_blocks))
        print("Number of pointer bocks: {}".format(num_pointer_blocks))
        print("Metadata size in blocks: {}".format(metadata_size))
        print("Metadata overhead: {} percent".format(metadata_overhead))
        print("effective artifice size: {}".format(effective_artifice_size))
        print("")

    return metadata_size

def calc_metadata_size_aont(blocks, parity, data, replicas, verbose):
    pointer_size = 4
    art_block_hash = 16

    amplification_factor = (parity + data)/data
    carrier_block_tuple = pointer_size + small_checksum
    record_size = (parity * carrier_block_tuple) + art_block_hash
    pointers_per_pointerblock = (block_size / pointer_size - 1)

    entries_per_block = math.floor(block_size / record_size)
    num_map_blocks = math.ceil((blocks / data) / entries_per_block)
    num_map_map_blocks = math.ceil((num_map_blocks / data) / entries_per_block)
    num_pointer_blocks = math.ceil(num_map_map_blocks / pointers_per_pointerblock)
    metadata_size = ((num_pointer_blocks + 1) * replicas) + num_map_map_blocks + num_map_blocks
    metadata_overhead = (metadata_size / blocks) * 100
    effective_artifice_size = (blocks * amplification_factor) + metadata_size

    if verbose == True:
        print("|---Metadata size for AONT-----|")
        print("Codeword configuration: {} data blocks, {} parity blocks".format(data, parity))
        print("Write amplification factor: {}".format(amplification_factor))
        print("Record Size: {} bytes".format(record_size))
        print("Entries per map block: {}".format(entries_per_block))
        print("Number of map blocks: {}".format(num_map_blocks))
        print("Number of map map blocks: {}".format(num_map_map_blocks))
        print("Number of pointer bocks: {}".format(num_pointer_blocks))
        print("Metadata size in blocks: {}".format(metadata_size))
        print("Metadata overhead: {} percent".format(metadata_overhead))
        print("effective artifice size: {}".format(effective_artifice_size))
        print("")

    return metadata_size


def calc_total_size_rs(size, parity, data, entropy):
    return (size * (parity / data)) + calc_metadata_size_rs(size, parity, entropy, data, 8, False)

def calc_total_size_sss(size, k, m):
    return (size * (m)) + calc_metadata_size_shamir(size, m, k, 8, False)

def calc_total_size_ssms(size, k, m):
    return (size * ((m+k)/k)) + calc_metadata_size_ssms(size, m, k, 8, False)

def calc_total_size_aont(size, parity, data):
    return (size * ((parity + data) / data)) + calc_metadata_size_aont(size, parity, data, 8, False)

def calc_comb_sss(freespace, size, k, m):
    if freespace == size:
        return k * freespace
    else: 
        return size * k


def calc_comb_ssms(freespace, size, k, m):
    if freespace == size:
        return k * freespace
    else:
        return size * k

def calc_comb_aont(freespace, size, parity, data):
    if freespace == size:
        return freespace * parity
    else: 
        return size * k

def main(args):

    calc_metadata_size_rs(def_art_size, 4, 1, 2, 8, True)
    calc_metadata_size_shamir(def_art_size, 8, 5, 8, True)
    calc_metadata_size_ssms(def_art_size, 8, 5, 8, True)
    calc_metadata_size_aont(def_art_size, 8, 5, 8, True)
    
    if args[1] == "shares":
        m_values = np.arange(3, 10, 1)
        prob1 = []
        for i in m_values:
            #1 entropy block, 1 data block, i parity blocks
            prob1.append(calc_comb_sss(def_free_blocks, def_art_size, i, 10))
            #reconstruct threshold of 2, i additional blocks

        plt.xlabel("Reconstruction Threshold")
        plt.ylabel("Number of possible combinations (2^x)")
        plt.title("Possible combinations versus Reconstruction Threshold")
        shamir = plt.plot(m_values, prob1, label='SSS', marker="x")
        plt.legend()
        plt.show()
    elif args[1] == "freespace":
        m_values = np.power(2, np.arange(24, 27, 0.01))
        prob1 = []
        for i in m_values:
            #1 entropy block, 1 data block, i parity blocks
            prob1.append(calc_comb_sss(def_free_blocks, def_art_size, i, 10))
            #reconstruct threshold of 2, i additional blocks

        plt.xlabel("Size of Freespace (blocks)")
        plt.ylabel("Number of possible combinations (2^x)")
        plt.title("Possible combinations versus Freespace Size")
        shamir = plt.plot(m_values, prob1, label='SSS', marker="x")
        plt.legend()
        plt.show()
    elif args[1] == "size":
        m_values = np.power(2, np.arange(18, 21, 0.01))
        prob1 = []
        for i in m_values:
            #1 entropy block, 1 data block, i parity blocks
            prob1.append(calc_comb_sss(def_free_blocks, def_art_size, i, 10))
            #reconstruct threshold of 2, i additional blocks

        plt.xlabel("Size of Artifice Volume (blocks)")
        plt.ylabel("Number of possible combinations (2^x)")
        plt.title("Possible combinations versus Artifice Size")
        shamir = plt.plot(m_values, prob1, label='SSS', marker="x")
        plt.legend()
        plt.show()

    else:
        print("Invalid argument")


if __name__ == "__main__":
    if len(sys.argv) == 2:
        main(sys.argv)
    else:
        print("Wrong number of arguments")
