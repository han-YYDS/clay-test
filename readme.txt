# compile
$> ./compile.sh

# print usage
$> ./build/Tester

# test

We can test for n=4, k=2, w=4. We first encode, and then simulate to repair
block with the block idx = 0 (e.g., repair index = 0), which is the first data
block out of the four blocks. We set the packet size to be 1 KiB, and the block
size to be 1 MiB.

$> ./build/Tester 4 2 4 1 1 0
