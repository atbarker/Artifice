#!/usr/bin/python3
import numpy as np
import math
import sys
from scipy.stats import binom
from scipy.interpolate import interp1d
import matplotlib.pyplot as plt

block_size = 4096 #4KB
def_total_size = 549755813888 #512GB
def_free_blocks = def_total_size / block_size
def_overwritten = 1310720 #5GB in blocks
def_art_size = 1310720 #5GB artifice instance
prob_success = def_overwritten / def_free_blocks
num_days = 365
small_checksum = 0 #checksum used to verify carrier block integrity


#in mathematica this is represented as Sum[PDF[BinomialDistribution[k+m,p],i], {i, 0, m}]
#as this is just a cumulative distribution function, python can do this for us
def prob_survival_sss(k, m, p):
    #return binom.cdf(m, k+m, p)
    return binom.cdf(m-k, m, p)

def prob_survival_rs(e, d, m, p):
    return binom.cdf(m-d, e+m, p)

def prob_survival_aont(d, m, p):
    return binom.cdf(m, m+d, p)

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

def prob_metadata_alive_rs(e, d, m, art_size, blocks_over, free_blocks):
    prob = blocks_over / free_blocks
    metadata = calc_metadata_size_rs(art_size, m, e, d, 8, False)
    return math.pow(prob_survival_rs(e, d, m, prob), (metadata * num_days))

#in this case k is the reconstruct threshold, m is the number of additional shares
def prob_metadata_alive_sss(k, m, art_size, blocks_over, free_blocks):
    prob = blocks_over / free_blocks
    metadata = calc_metadata_size_shamir(art_size, m, k, 8, False)
    return math.pow(prob_survival_sss(k, m, prob), (metadata * num_days))

def prob_metadata_alive_ssms(k, m, art_size, blocks_over, free_blocks):
    prob = blocks_over / free_blocks
    metadata = calc_metadata_size_ssms(art_size, m, k, 8, False)
    return math.pow(prob_survival_sss(k, m, prob), (metadata * num_days))

def prob_metadata_alive_aont(d, m, art_size, blocks_over, free_blocks):
    prob = blocks_over / free_blocks
    metadata = calc_metadata_size_aont(art_size, m, d, 8, False)
    return math.pow(prob_survival_aont(d, m, prob), (metadata * num_days))


#k is data + entropy, m 
def prob_artifice_alive_rs(e, d, m, art_size, blocks_over, free_blocks):
    prob = blocks_over / free_blocks
    total_size = calc_total_size_rs(art_size, m, d, e)
    return math.pow(prob_survival_rs(e, d, m, prob), (total_size * num_days))

#k is the reconstruct threshold, m is the number of additional shares
def prob_artifice_alive_sss(k, m, art_size, blocks_over, free_blocks):
    prob = blocks_over / free_blocks
    total_size = calc_total_size_sss(art_size, m, k)
    return math.pow(prob_survival_sss(k, m, prob), (total_size * num_days))

def prob_artifice_alive_ssms(k, m, art_size, blocks_over, free_blocks):
    prob = blocks_over / free_blocks
    total_size = calc_total_size_ssms(art_size, m, k)
    return math.pow(prob_survival_sss(k, m, prob), (total_size * num_days))

def prob_artifice_alive_aont(d, m, art_size, blocks_over, free_blocks):
    prob = blocks_over / free_blocks
    total_size = calc_total_size_aont(art_size, m, d)
    return math.pow(prob_survival_aont(d, m, prob), (total_size * num_days))

def prob_nines(k, m, p):
    return -math.log10(1 - prob_survival(k, m, p))

#mean time to failure (MTTF) is in hours
#calculates the probability that a given disk will die
def prob_disk_alive(mttf, days):
    return ((1 - (1/mttf)) ** (days * 24))

def main(args):

    calc_metadata_size_rs(def_art_size, 4, 1, 2, 8, True)
    calc_metadata_size_shamir(def_art_size, 8, 5, 8, True)
    calc_metadata_size_ssms(def_art_size, 8, 5, 8, True)
    calc_metadata_size_aont(def_art_size, 8, 5, 8, True)
    
    if args[1] == "nines":
        m_max = 0.05
        m_values = np.arange(0.0001, 0.05, 0.0001)
        prob1 = []
        prob2 = []
        prob3 = []
        prob4 = []
        prob5 = []
        for i in m_values:
            prob1.append(prob_nines(3, 3, i))
            prob2.append(prob_nines(3, 4, i))
            prob3.append(prob_nines(3, 5, i))
            prob4.append(prob_nines(3, 6, i))
            prob5.append(prob_nines(3, 7, i))
        plt.axis([0, 0.05, 0, 14])
        plt.plot(m_values, prob1, m_values, prob2, m_values, prob3, m_values, prob4, m_values, prob5)
        plt.show()

    #probability of survival for metadata, reed solomon
    elif args[1] == "metadata":
        m_values = np.arange(3, 10, 1)
        prob1 = []
        prob2 = []
        prob3 = []
        prob4 = []
        prob5 = []
        prob6 = []
        prob7 = []
        prob8 = []
        for i in m_values:
            #1 entropy block, 1 data block, i parity blocks
            #in this case at least i-2 blocks must survive, the threshold is tied to the 
            #number of carrier blocks in the keyword
            prob1.append(prob_metadata_alive_rs(1, 1, i, def_art_size, def_overwritten, def_free_blocks))
            #reconstruct threshold of 2, i additional blocks
            prob2.append(prob_metadata_alive_sss(2, i, def_art_size, def_overwritten, def_free_blocks))
            #1 entropy block, 2 data blocks, i parity blocks
            prob3.append(prob_metadata_alive_rs(1, 2, i, def_art_size, def_overwritten, def_free_blocks))
            #reconstruct threshold of 3, i additional blocks
            prob4.append(prob_metadata_alive_sss(3, i, def_art_size, def_overwritten, def_free_blocks))
            prob5.append(prob_metadata_alive_ssms(2, i, def_art_size, def_overwritten, def_free_blocks))
            prob6.append(prob_metadata_alive_ssms(3, i, def_art_size, def_overwritten, def_free_blocks))
            prob7.append(prob_metadata_alive_aont(2, i, def_art_size, def_overwritten, def_free_blocks))
            prob8.append(prob_metadata_alive_aont(3, i, def_art_size, def_overwritten, def_free_blocks))

        plt.xlabel("Number of Carrier Blocks")
        plt.ylabel("Probability of Survival")
        plt.title("Probability of Metadata Survival vs Number of Carrier Blocks")
        rs = plt.plot(m_values, prob1, label='RS, 1 data block', marker="o")
        rs2 = plt.plot(m_values, prob3, label='RS, 2 data blocks', marker="s")
        shamir = plt.plot(m_values, prob2, label='SSS, threshold 2', marker="D")
        shamir2 = plt.plot(m_values, prob4, label='SSS, threshold 3', marker="p")
        #ssms = plt.plot(m_values, prob5, label='SSMS, threshold 2', marker="x")
        #ssms2 = plt.plot(m_values, prob6, label='SSMS, threshold 3', marker="+")
        aont = plt.plot(m_values, prob7, label='AONT, threshold 2', marker="x")
        aont2 = plt.plot(m_values, prob8, label='AONT, threshold 3', marker="+")
        plt.legend()
        plt.show()

    elif args[1] == "all":
        m_values = np.arange(3, 10, 1)
        prob1 = []
        prob2 = []
        prob3 = []
        prob4 = []
        prob5 = []
        prob6 = []
        prob7 = []
        prob8 = []
        for i in m_values:
            #1 entropy block, 1 data block, i parity blocks
            prob1.append(prob_artifice_alive_rs(1, 1, i, def_art_size, def_overwritten, def_free_blocks))
            #reconstruct threshold of 2, i additional blocks
            prob2.append(prob_artifice_alive_sss(2, i, def_art_size, def_overwritten, def_free_blocks))
            #1 entropy block, 2 data blocks, i parity blocks
            prob3.append(prob_artifice_alive_rs(1, 2, i, def_art_size, def_overwritten, def_free_blocks))
            #reconstruct threshold of 3, i additional blocks
            prob4.append(prob_artifice_alive_sss(3, i, def_art_size, def_overwritten, def_free_blocks))
            #reconstruct threshold of 4, i additional shares
            prob5.append(prob_artifice_alive_ssms(2, i, def_art_size, def_overwritten, def_free_blocks))
            prob6.append(prob_artifice_alive_ssms(3, i, def_art_size, def_overwritten, def_free_blocks))
            prob7.append(prob_artifice_alive_aont(2, i, def_art_size, def_overwritten, def_free_blocks))
            prob8.append(prob_artifice_alive_aont(3, i, def_art_size, def_overwritten, def_free_blocks))


        plt.xlabel("Number of Carrier Blocks")
        plt.ylabel("Probability of Survival")
        plt.title("Probability of Artifice Survival vs Number of Carrier Blocks")
        rs = plt.plot(m_values, prob1, label='RS, 1 data block', marker="o")
        rs2 = plt.plot(m_values, prob3, label='RS, 2 data blocks', marker="s")
        shamir = plt.plot(m_values, prob2, label='SSS, threshold 2', marker="D")
        shamir2 = plt.plot(m_values, prob4, label='SSS, threshold 3', marker="p")
        #ssms = plt.plot(m_values, prob5, label='SSMS, threshold 2', marker="x")
        #ssms2 = plt.plot(m_values, prob6, label='SSMS, threshold 3', marker="+")
        aont = plt.plot(m_values, prob7, label='AONT, threshold 2', marker="x")
        aont2 = plt.plot(m_values, prob8, label='AONT, threshold 3', marker="+")
        plt.legend()
        plt.show()

    elif args[1] == "disk":
        print("Probability of the disk being alive {}".format(prob_disk_alive(200000, 365)))

    elif args[1] == "volume":
        print("Graph for the probability of survival based on Artifice volume size")
        #256MB to 4GB volume in powers of 2 (2^29, 2^30, 2^31, ...)
        m_values = np.power(2, np.arange(16, 21, 0.01)) 
        prob1 = []
        prob2 = []
        prob3 = []
        prob4 = []
        prob5 = []
        prob6 = []

        for i in m_values:
            prob1.append(prob_artifice_alive_rs(1, 1, 8, i, def_overwritten, def_free_blocks))
            prob2.append(prob_artifice_alive_sss(2, 8, i, def_overwritten, def_free_blocks))
            prob3.append(prob_artifice_alive_aont(2, 8, i, def_overwritten, def_free_blocks))
            prob4.append(prob_artifice_alive_rs(1, 2, 8, i, def_overwritten, def_free_blocks))
            prob5.append(prob_artifice_alive_sss(3, 8, i, def_overwritten, def_free_blocks))
            prob6.append(prob_artifice_alive_aont(3, 8, i, def_overwritten, def_free_blocks))           

        plt.xlabel("Size of the Artifice Volume (blocks)")
        plt.ylabel("Probability of Survival")
        plt.title("Probability of Artifice Survival vs Size of the Artifice Volume")
        rs = plt.plot(m_values, prob1, label='RS, 1 data block')
        rs2 = plt.plot(m_values, prob4, label='RS, 2 data blocks')
        shamir = plt.plot(m_values, prob2, label='SSS, threshold 2')
        #shamir2 = plt.plot(m_values, prob5, label='SSS, threshold 3')
        aont = plt.plot(m_values, prob3, label='AONT, threshold 2')
        aont2 = plt.plot(m_values, prob6, label='AONT, threshold 3')
        plt.legend()
        plt.show()

    elif args[1] == "write":
        print("Graph for amount written per day")
        #256MB to 8GB
        m_values = np.power(2, np.arange(16, 21, 0.01))
        prob1 = []
        prob2 = []
        prob3 = []
        prob4 = []
        prob5 = []
        prob6 = []

        for i in m_values:
            prob1.append(prob_artifice_alive_rs(1, 1, 8, def_art_size, i, def_free_blocks))
            prob2.append(prob_artifice_alive_sss(2, 8, def_art_size, i, def_free_blocks))
            prob3.append(prob_artifice_alive_aont(2, 8, def_art_size, i, def_free_blocks))
            prob4.append(prob_artifice_alive_rs(1, 2, 8, def_art_size, i, def_free_blocks))
            prob5.append(prob_artifice_alive_sss(3, 8, def_art_size, i, def_free_blocks))
            prob6.append(prob_artifice_alive_aont(3, 8, def_art_size, i, def_free_blocks))

        plt.xlabel("Amount written per day (blocks)")
        plt.ylabel("Probability of Survival")
        plt.title("Probability of Artifice Surivival vs Amount Written per Day")
        rs = plt.plot(m_values, prob1, label='RS, 1 data block')
        rs2 = plt.plot(m_values, prob4, label='RS, 2 data blocks')
        shamir = plt.plot(m_values, prob2, label='SSS, threshold 2')
        #shamir2 = plt.plot(m_values, prob5, label='SSS, threshold 3')
        aont = plt.plot(m_values, prob3, label='AONT, threshold 2')
        aont2 = plt.plot(m_values, prob6, label='AONT, threshold 3')
        plt.legend()
        plt.show()

    elif args[1] == "freespace":
        print("Graph for size based on the amount of free space")
        #64GB to 1TB
        m_values = np.power(2, np.arange(24, 29, 0.01))
        prob1 = []
        prob2 = []
        prob3 = []
        prob4 = []
        prob5 = []
        prob6 = []

        for i in m_values:
            prob1.append(prob_artifice_alive_rs(1, 1, 8, def_art_size, def_overwritten, i))
            prob2.append(prob_artifice_alive_sss(2, 8, def_art_size, def_overwritten, i))
            prob3.append(prob_artifice_alive_aont(2, 8, def_art_size, def_overwritten, i))
            prob4.append(prob_artifice_alive_rs(1, 2, 8, def_art_size, def_overwritten, i))
            prob5.append(prob_artifice_alive_sss(3, 8, def_art_size, def_overwritten, i))
            prob6.append(prob_artifice_alive_aont(3, 8, def_art_size, def_overwritten, i))

        plt.xlabel("Unallocated Space (blocks)")
        plt.ylabel("Probability of Survival")
        plt.title("Probability of Artifice Survival vs Size of Unallocated Space")
        rs = plt.plot(m_values, prob1, label='RS, 1 data block')
        rs2 = plt.plot(m_values, prob4, label='RS, 2 data blocks')
        shamir = plt.plot(m_values, prob2, label='SSS, threshold 2')
        #shamir2 = plt.plot(m_values, prob5, label='SSS, threshold 3')
        aont = plt.plot(m_values, prob3, label='AONT, threshold 2')
        aont2 = plt.plot(m_values, prob6, label='AONT, threshold 3')
        plt.legend()
        plt.show()

    elif args[1] == "overhead":
        print("Graph for size of the artifice volume")
        #vary by number of carrier blocks, include an equation used to generate the graph
        #do we modulate the threshold or the total number of carrier blocks? In the case of SSMS or AONT then it will vary depending on the threshold
        #For secret sharing we get a simple linear relationship, the size of the metadata raises linearly and the metadata structures are fixed
        #In the case of AONT or SSMS we see the size complexity increase by a factor of threshold/
        #metadata could possibly grow and shrink depending on how many blocks are in use (due to high overhead).
        m_values = np.power(2, np.arange(24, 29, 0.01))
        prob1 = []
        prob2 = []
        prob3 = []
        prob4 = []
        prob5 = []
        prob6 = []

        for i in m_values:
            prob1.append(prob_artifice_alive_rs(1, 1, 8, def_art_size, def_overwritten, i))
            prob2.append(prob_artifice_alive_sss(2, 8, def_art_size, def_overwritten, i))
            prob3.append(prob_artifice_alive_aont(2, 8, def_art_size, def_overwritten, i))
            prob4.append(prob_artifice_alive_rs(1, 2, 8, def_art_size, def_overwritten, i))
            prob5.append(prob_artifice_alive_sss(3, 8, def_art_size, def_overwritten, i))
            prob6.append(prob_artifice_alive_aont(3, 8, def_art_size, def_overwritten, i))

        plt.xlabel("Unallocated Space (blocks)")
        plt.ylabel("Probability of Survival")
        plt.title("Probability of Artifice Survival vs Size of Unallocated Space")
        rs = plt.plot(m_values, prob1, label='RS, 1 data block')
        rs2 = plt.plot(m_values, prob4, label='RS, 2 data blocks')
        shamir = plt.plot(m_values, prob2, label='SSS, threshold 2')
        #shamir2 = plt.plot(m_values, prob5, label='SSS, threshold 3')
        aont = plt.plot(m_values, prob3, label='AONT, threshold 2')
        aont2 = plt.plot(m_values, prob6, label='AONT, threshold 3')
        plt.legend()
        plt.show()

    else:
        print("Invalid argument")


if __name__ == "__main__":
    if len(sys.argv) == 2:
        main(sys.argv)
    else:
        print("Wrong number of arguments")
