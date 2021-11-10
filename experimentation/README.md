# Artifice Experiments

## Entropy-test

This is a basic kernel module for testing reading data from a file system from the kernel.

The header and .c files contain code used to retrieve entropy data from the public file system if a Reed-Solomon+Entropy IDA is desired. The intent is that the user would specify some directory containing pseudo-random files (something encrypted, DRM protected, etc) which would form the seed for a CSPRNG. The generated entropy blocks are used in the IDA as a one time pad of sorts. Since these blocks are part of the Reed-Solomon codeword Artifice can tolerate loss of *some* entropy blocks.

***WARNING*** The technique is considered dangerous and ill advised by a vast majority of OS developers (as far as I know). One is only supposed to interact with the proc pseudo file system from within the kernel and never touch any userspace files.  Given the covert nature and general hackiness of steganography in general and that we are only *reading* data from a mounted volume, we consider this technique to be fair game in *only* this scenario. It is our opinion that *writing* data to a mounted volume outside of the proc file system is dangerous and could compromise deniability.

## Reed-Solomon

A variety of Reed-Solomon erasure coding implementations. Mostly messing around. None of that code is currently used.

