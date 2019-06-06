#!/usr/bin/python3
import numpy as np
import math
from scipy.stats import binom
import matplotlib.pyplot as plt

block_size = 4096 #4KB
total_size = 54975581388 #512GB
num_blocks = total_size / block_size
blocks_overwritten = 1310720 #5GB in blocks
art_size_blocks = 1310720 #5GB artifice instance
prob_success = blocks_overwritten / num_blocks
num_days = 365


#in matlab this is represented as Sum[PDF[BinomialDistribution[k+m,p],i], {i, 0, m}]
#as this is just a cumulative distribution function, python can do this for us
def prob_survival(k, m, p):
    return binom.cdf(m, k+m, p)

def prob_survival_artifice(art_size, k, m, p):
    return (prob_survival(k, m, p)) ** art_size_blocks

def calc_metadata_size_rs(blocks, parity, entropy, data, replicas):
    pointer_size = 4
    small_checksum = 2
    art_block_hash = 16
    entropy_filename_hash = 8
    print("Codeword configuration: {} data blocks, {} entropy, {} parity blocks".format(data, entropy, parity))

    amplification_factor = parity / data
    print("write amplification factor: {}".format(amplification_factor))
    carrier_block_tuple = pointer_size + small_checksum
    #I forget what the 4 is supposed to do, probably to align everything to an easy block boundary
    record_size = (parity * carrier_block_tuple) + (entropy * pointer_size) + art_block_hash + entropy_filename_hash
    print("record size: {}".format(record_size))
    pointers_per_pointerblock = (block_size / pointer_size - 1)

    entries_per_block = math.floor(block_size / record_size)
    print("entries per block: {}".format(entries_per_block))
    num_map_blocks = math.ceil((blocks / data) / entries_per_block)
    print("number of map blocks: {}".format(num_map_blocks))
    num_map_map_blocks = math.ceil((num_map_blocks / data) / entries_per_block)
    print("number of map map blocks: {}".format(num_map_map_blocks))
    num_pointer_blocks = math.ceil(num_map_map_blocks / pointers_per_pointerblock)
    print("number of pointer_blocks: {}".format(num_pointer_blocks))
    metadata_size = ((num_pointer_blocks + 1) * replicas) + num_map_map_blocks + num_map_blocks
    print("total size in blocks: {}".format(metadata_size))
    metadata_overhead = (metadata_size / blocks) * 100
    print("metadata overhead: {} percent".format(metadata_overhead))
    effective_artifice_size = (art_size_blocks * amplification_factor) + metadata_size
    print("effective artifice size: {}".format(effective_artifice_size))
    return metadata_size


def calc_metadata_size_shamir(blocks, parity, data, replicas):
    pointer_size = 4
    small_checksum = 2
    art_block_hash = 16

    print("Codeword configuration: {} data blocks, {} parity blocks".format(data, parity))

    amplification_factor = parity / data
    print("write amplification factor: {}".format(amplification_factor))
    carrier_block_tuple = pointer_size + small_checksum
    record_size = (parity * carrier_block_tuple) + art_block_hash
    print("record size: {}".format(record_size))
    pointers_per_pointerblock = (block_size / pointer_size) - 1

    entries_per_block = math.floor(block_size / record_size)
    print("entries per block: {}".format(entries_per_block))
    num_map_blocks = math.ceil(blocks / entries_per_block)
    print("number of map blocks: {}".format(num_map_blocks))
    num_map_map_blocks = math.ceil(num_map_blocks / entries_per_block)
    print("number of map map blocks: {}".format(num_map_map_blocks))
    num_pointer_blocks = math.ceil(num_map_map_blocks / pointers_per_pointerblock)
    print("number of pointer_blocks: {}".format(num_pointer_blocks))
    metadata_size = ((num_pointer_blocks + 1) * replicas) + num_map_map_blocks + num_map_blocks
    print("total size in blocks: {}".format(metadata_size))
    metadata_overhead = (metadata_size / blocks) * 100
    print("metadata overhead: {} percent".format(metadata_overhead))
    effective_artifice_size = (art_size_blocks * amplification_factor) + metadata_size
    print("effective artifice size: {}".format(effective_artifice_size))
    return metadata_size

def calc_total_size(size, parity, data):
    return size * (parity / data)

def prob_metadata_alive(k, m):
    return (prob_survival(k, m, prob_success) ** (calc_metadata_size_rs(art_size_blocks, m, 2, 2, 8) * num_days))

def prob_artifice_alive(k, m):
    return (prob_survival(k, m, prob_success) ** (calc_total_size(art_size_blocks, m, k, 8) * num_days))

#mean time to failure (MTTF) is in hours
def prob_disk_alive(mttf, days):
    return ((1 - (1/mttf)) ** (days * 24))

def main():
    print("|---Metadata size for Reed-Solomon-----|")
    calc_metadata_size_rs(art_size_blocks, 4, 1, 2, 8)
    print(" ")
    print("|---Metadata size for secret sharing---|")
    calc_metadata_size_shamir(art_size_blocks, 4, 1, 8)

if __name__ == "__main__":
    main()
