Erasure coding implementation
=============================

This document provides information about how [erasure code][1] has
been implemented into ec translator. It describes the algorithm used
and the optimizations made, but it doesn't contain a full description
of the mathematical background needed to understand erasure coding in
general. It either describes the other parts of ec not directly
related to the encoding/decoding procedure, like synchronization or
fop management.


Introduction
------------

EC is based on [Reed-Solomon][2] erasure code. It's a very old code.
It's not considered the best one nowadays, but is good enough and it's
one of the few codes that is not covered by any patent and can be
freely used.

To define the Reed-Solomon code we use 3 parameters:

  * __Key fragments (K)__  
    It represents the minimum number of healthy fragments that will be
    needed to be able to recover the original data. Any subset of K
    out of the total number of fragments will serve.

  * __Redundancy fragments (R)__  
    It represents the number of extra fragments to compute for each
    original data block. This value determines how many fragments can
    be lost before being unable to recover the original data.

  * __Fragment size (S)__  
    This determines the size of each fragment. The original data
    block size is computed as S * K. Currently this values is fixed
    to 512 bytes.

  * __Total number of fragments (N = K + R)__  
    This isn't a real parameter but it will be useful to simplify
    the following descriptions.

From the point of view of the implementation, it only consists on
matrix multiplications. There are two kinds of matrices to use for
Reed-Solomon:

  * __[Systematic][3]__  
    This kind of matrix has the particularity that K of the encoded
    fragments are simply a copy of the original data, divided into K
    pieces. Thus no real encoding needs to be done for them and only
    the R redundancy fragments need to be computed.

    This kind of matrices contain one KxK submatrix that is the
    [identity matrix][4].

  * __Non-systematic__  
    This kind of matrix doesn't contain an identity submatrix. This
    means that all of the N fragments need to be encoded, requiring
    more computation. On the other hand, these matrices have some nice
    properties that allow faster implementations of some algorithms,
    like the matrix inversion used to decode the data.

    Another advantage of non-systematic matrices is that the decoding
    time is constant, independently of how many fragments are lost,
    while systematic approach can suffer from performance degradation
    when one fragment is lost.

All non-systematic matrices can be converted to systematic ones, but
then we lose the good properties of the non-systematic. We have to
choose betwee best peek performance (systematic) and performance
stability (non-systematic).


Encoding procedure
------------------

To encode a block of data we need a KxN matrix where each subset of K
rows is [linearly independent][5]. In other words, the determinant of
each KxK submatrix is not 0.

There are some known ways to obtain this kind of matrices. EC uses a
small variation of a matrix known as [Vandermonde Matrix][6] where
each element of the matrix is defined as:

    a(i, j) = i ^ (K - j)

    where i is the row from 1 to N, and j is the column from 1 to K.

This is exactly the Vandermonde Matrix but with the elements of each
row in reverse order. This change is made to be able to implement a
small optimization in the matrix multiplication.

Once we have the matrix, we only need to compute the multiplication
of this matrix by a vector composed of K elements of data coming from
the original data block.

     /                 \             /                               \
    |    1   1  1  1  1 |    / \    |     a +    b +   c +  d + e = t |
    |   16   8  4  2  1 |   | a |   |   16a +   8b +  4c + 2d + e = u |
    |   81  27  9  3  1 |   | b | = |   81a +  27b +  9c + 3d + e = v |
    |  256  64 16  4  1 | * | c |   |  256a +  64b + 16c + 4d + e = w |
    |  625 125 25  5  1 |   | d |   |  625a + 125b + 25c + 5d + e = x |
    | 1296 216 36  6  1 |   | e |   | 1296a + 216b + 36c + 6d + e = y |
    | 2401 343 49  7  1 |    \ /    | 2401a + 343b + 49c + 7d + e = z |
     \                 /             \                               /

The optimization that can be done here is this:

    16a + 8b + 4c + 2d + e = 2(2(2(2a + b) + c) + d) + e

So all the multiplications are always by the number of the row (2 in
this case) and we don't need temporal storage for intermediate
results:

    a *= 2
    a += b
    a *= 2
    a += c
    a *= 2
    a += d
    a *= 2
    a += e

Once we have the result vector, each element is a fragment that needs
to be stored in a separate place.


Decoding procedure
------------------

To recover the data we need exactly K of the fragments. We need to
know which K fragments we have (i.e. the original row number from
which each fragment was calculated). Once we have this data we build
a square KxK matrix composed by the rows corresponding to the given
fragments and invert it.

With the inverted matrix, we can recover the original data by
multiplying it with the vector composed by the K fragments.

In our previous example, if we consider that we have recovered
fragments t, u, v, x and z, corresponding to rows 1, 2, 3, 5 and 7,
we can build the following matrix:

     /                 \
    |    1   1  1  1  1 |
    |   16   8  4  2  1 |
    |   81  27  9  3  1 |
    |  625 125 25  5  1 |
    | 2401 343 49  7  1 |
     \                 /

And invert it:

     /                                     \
    |    1/48  -1/15    1/16  -1/48   1/240 |
    |  -17/48  16/15  -15/16  13/48 -11/240 |
    |  101/48 -86/15   73/16 -53/48  41/240 |
    | -247/48 176/15 -129/16  83/48 -61/240 |
    |   35/8   -7      35/8   -7/8    1/8   |
     \                                     /

Multiplying it by the vector (t, u, v, x, z) we recover the original
data (a, b, c, d, e):

     /                                     \     / \     / \
    |    1/48  -1/15    1/16  -1/48   1/240 |   | t |   | a |
    |  -17/48  16/15  -15/16  13/48 -11/240 |   | u |   | b |
    |  101/48 -86/15   73/16 -53/48  41/240 | * | v | = | c |
    | -247/48 176/15 -129/16  83/48 -61/240 |   | x |   | d |
    |   35/8   -7      35/8   -7/8    1/8   |   | z |   | e |
     \                                     /     \ /     \ /


Galois Field
------------

This encoding/decoding procedure is quite complex to compute using
regular mathematical operations and it's not well suited for what
we want to do (note that matrix elements can grow unboundly).

To solve this problem, exactly the same procedure is done inside a
[Galois Field][7] of characteristic 2, which is a finite field with
some interesting properties that make it specially useful for fast
operations using computers.

There are two main differences when we use this specific Galois Field:

  * __All regular additions are replaced by bitwise xor's__  
    For todays computers it's not really faster to execute an xor
    compared to an addition, however replacing additions by xor's
    inside a multiplication has many advantages (we will make use of
    this to optimize the multiplication).

    Another consequence of this change is that additions and
    substractions are really the same xor operation.

  * __The elements of the matrix are bounded__  
    The field uses a modulus that keep all possible elements inside
    a delimited region, avoiding really big numbers and fixing the
    number of bits needed to represent each value.

    In the current implementation EC uses 8 bits per field element.

It's very important to understand how multiplications are computed
inside a Galois Field to be able to understand how has it been
optimized.

We'll start with a simple 'old school' multiplication but in base 2.
For example, if we want to multiply 7 * 5 (111b * 101b in binary), we
do the following:

          1 1 1 (= 7)
    *     1 0 1 (= 5)
    -----------
          1 1 1 (= 7)
    +   0 0 0   (= 0)
    + 1 1 1     (= 7)
    -----------
    1 0 0 0 1 1 (= 35)

This is quite simple. Note that the addition of the third column
generates a carry that is propagated to all the other left columns.

The next step is to define the modulus of the field. Suppose we use
11 as the modulus. Then we convert the result into an element of the
field by dividing by the modulus and taking the residue. We also use
the 'old school' method in binary:


      1 0 0 0 1 1 (= 35) | 1 0 1 1 (= 11)
    - 0 0 0 0            ----------------
    ---------              0 1 1 (= 3)
      1 0 0 0 1
    -   1 0 1 1
    -----------
      0 0 1 1 0 1
    -     1 0 1 1
    -------------
          0 0 1 0 (= 2)

So, 7 * 5 in a field with modulus 11 is 2. Note that the main
objective in each iteration of the division is to make higher bits
equal to 0 when possible (if it's not possible in one iteration, it
will be zeroed on the next).

If we do the same but changing additions with xors we get this:

          1 1 1 (= 7)
    *     1 0 1 (= 5)
    -----------
          1 1 1 (= 7)
    x   0 0 0   (= 0)
    x 1 1 1     (= 7)
    -----------
      1 1 0 1 1 (= 27)

In this case, the xor of the third column doesn't generate any carry.

Now we need to divide by the modulus. We can also use 11 as the
modulus since it still satisfies the needed conditions to work on a
Galois Field of characteristic 2 with 3 bits:

      1 1 0 1 1 (= 27) | 1 0 1 1 (= 11)
    x 1 0 1 1          ----------------
    ---------            1 1 1 (= 7)
      0 1 1 0 1
    x   1 0 1 1
    -----------
        0 1 1 0 1
    x     1 0 1 1
    -------------
          0 1 1 0 (= 6)

Note that, in this case, to make zero the higher bit we need to
consider the result of the xor operation, not the addition operation.

So, 7 * 5 in a Galois Field of 3 bits with modulus 11 is 6.


Optimization
------------

To compute all these operations in a fast way some methods have been
traditionally used. Maybe the most common is the [lookup table][8].

The problem with this method is that it requires 3 lookups for each
byte multiplication, greatly amplifying the needed memory bandwidth
and making it difficult to take advantage of any SIMD support on the
processor.

What EC does to improve the performance is based on the following
property (using the 3 bits Galois Field of the last example):

    A * B mod N = (A * b{2} * 4 mod N) x
                  (A * b{1} * 2 mod N) x
                  (A * b{0} mod N)

This is basically a rewrite of the steps made in the previous example
to multiply two numbers but moving the modulus calculation inside each
intermediate result. What we can see here is that each term of the
xor can be zeroed if the corresponding bit of B is 0, so we can ignore
that factor. If the bit is 1, we need to compute A multiplied by a
power of two and take the residue of the division by the modulus. We
can precompute these values:

    A0 = A (we don't need to compute the modulus here)
    A1 = A0 * 2 mod N
    A2 = A1 * 2 mod N

Having these values we only need to add those corresponding to bits
set to 1 in B. Using our previous example:

    A = 1 1 1 (= 7)
    B = 1 0 1 (= 5)

    A0 = 1 1 1 (= 7)
    A1 = 1 1 1 * 1 0 mod 1 0 1 1 = 1 0 1 (= 5)
    A2 = 1 0 1 * 1 0 mod 1 0 1 1 = 0 0 1 (= 1)

    Since only bits 0 and 2 are 1 in B, we add A0 and A2:

    A0 + A2 = 1 1 1 x 0 0 1 = 1 1 0 (= 6)

If we carefully look at what we are doing when computing each Ax, we
see that we do two basic things:

  - Shift the original value one bit to the left
  - If the highest bit is 1, xor with the modulus

Let's write this in a detailed way (representing each bit):

    Original value:      a{2} a{1} a{0}
    Shift 1 bit:    a{2} a{1} a{0}  0

    If a{2} is 0 we already have the result:
        a{1} a{0} 0

    If a{2} is 1 we need to xor with the modulus:
        1 a{1} a{0} 0 x 1 0 1 1 = a{1} (a{0} x 1) 1

An important thing to see here is that if a{2} is 0, we can get the
same result by xoring with all 0 instead of the modulus. For this
reason we can rewrite the modulus as this:

    Modulus: a{2} 0 a{2} a{2}

This means that the modulus will be 0 0 0 0 is a{2} is 0, so the value
won't change, and it will be 1 0 1 1 if a{2} is 1, giving the correct
result. So, the computation is simply:

    Original value:      a{2} a{1} a{0}
    Shift 1 bit:    a{2} a{1} a{0}  0
    Apply modulus:  a{1} (a{0} x a{2}) a{2}

We can compute all Ax using this method. We'll get this:

    A0 = a{2} a{1} a{0}
    A1 = a{1} (a{0} x a{2}) a{2}
    A2 = (a{0} x a{2}) (a{1} x a{2}) a{1}

Once we have all terms, we xor the ones corresponding to the bits set
to 1 in B. In out example this will be A0 and A2:

   Result: (a{2} x a{0} x a{2}) (a{1} x a{1} x a{2}) (a{0} x a{1})

We can easily see that we can remove some redundant factors:

   Result: a{0} a{2} (a{0} x a{1})

This way we have come up with a simply set of equations to compute the
multiplication of any number by 5. If A is 1 1 1 (= 7), the result
must be 1 1 0 (= 6) using the equations, as we expected. If we try
another numbe for A, like 0 1 0 (= 2), the result must be 0 0 1 (= 1).

This seems a really fast way to compute the multiplication without
using any table lookup. The problem is that this is only valid for
B = 5. For other values of B another set of equations will be found.
To solve this problem we can pregenerate the equations for all
possible values of B. Since the Galois Field we use is small, this is
feasible.

One thing to be aware of is that, in general, two equations for
different bits of the same B can share common subexpressions. This
gives space for further optimizations to reduce the total number of
xors used in the final equations for a given B. However this is not
easy to find, since finding the smallest number of xors that give the
correct result is an NP-Problem. For EC an exhaustive search has been
made to find the best combinations for each possible value.


Implementation
--------------

All this seems great from the hardware point of view, but implementing
this using normal processor instructions is not so easy because we
would need a lot of shifts, ands, xors and ors to move the bits of
each number to the correct position to compute the equation and then
another shift to put each bit back to its final place.

For example, to implement the functions to multiply by 5, we would
need something like this:

    Bit 2:  T2 = (A & 1) << 2
    Bit 1:  T1 = (A & 4) >> 1
    Bit 0:  T0 = ((A >> 1) x A) & 1
    Result: T2 + T1 + T0

This doesn't look good. So here we make a change in the way we get
and process the data: instead of reading full numbers into variables
and operate with them afterwards, we use a single independent variable
for each bit of the number.

Assume that we can read and write independent bits from memory (later
we'll see how to solve this problem when this is not possible). In
this case, the code would look something like this:

    Bit 2:        T2 = Mem[2]
    Bit 1:        T1 = Mem[1]
    Bit 0:        T0 = Mem[0]
    Computation:  T1 ^= T0
    Store result: Mem[2] = T0
                  Mem[1] = T2
                  Mem[0] = T1

Note that in this case we handle the final reordering of bits simply
by storing the right variable to the right place, without any shifts,
ands nor ors. In fact we only have memory loads, memory stores and
xors. Note also that we can do all the computations directly using the
variables themselves, without additional storage. This true for most
of the values, but in some cases an additional one or two temporal
variables will be needed to store intermediate results.

The drawback of this approach is that additions, that are simply a
xor of two numbers will need as many xors as bits are in each number.


SIMD optimization
-----------------

So we have a good way to compute the multiplications, but even using
this we'll need several operations for each byte of the original data.
We can improve this by doing multiple multiplications using the same
set of instructions.

With the approach taken in the implementation section, we can see that
in fact it's really easy to add SIMD support to this method. We only
need to store in each variable one bit from multiple numbers. For
example, when we load T2 from memory, instead of reading the bit 2 of
the first number, we can read the bit 2 of the first, second, third,
fourth, ... numbers. The same can be done when loading T1 and T0.

Obviously this needs to have a special encoding of the numbers into
memory to be able to do that in a single operation, but since we can
choose whatever encoding we want for EC, we have chosen to have
exactly that. We interpret the original data as a stream of bits, and
we split it into subsequences of length L, each containing one bit of
a number. Every S subsequences form a set of numbers of S bits that
are encoded and decoded as a single group. This repeats for any
remaining data.

For example, in a simple case with L = 8 and S = 3, the original data
would contain something like this (interpreted as a sequence of bits,
offsets are also bit-based):

    Offset  0: a{0} b{0} c{0} d{0} e{0} f{0} g{0} h{0}
    Offset  8: a{1} b{1} c{1} d{1} e{1} f{1} g{1} h{1}
    Offset 16: a{2} b{2} c{2} d{2} e{2} f{2} g{2} h{2}
    Offset 24: i{0} j{0} k{0} l{0} m{0} n{0} o{0} p{0}
    Offset 32: i{1} j{1} k{1} l{1} m{1} n{1} o{1} p{1}
    Offset 40: i{2} j{2} k{2} l{2} m{2} n{2} o{2} p{2}

Note: If the input file is not a multiple of S * L, 0-padding is done.

Here we have 16 numbers encoded, from A to P. This way we can easily
see that reading the first byte of the file will read all bits 0 of
number A, B, C, D, E, F, G and H. The same happens with bits 1 and 2
when we read the second and third bytes respectively. Using this
encoding and the implementation described above, we can see that the
same set of instructions will be computing the multiplication of 8
numbers at the same time.

This can be further improved if we use L = 64 with 64 bits variables
on 64-bits processor. It's even faster if we use L = 128 using SSE
registers or L = 256 using AVX registers on Intel processors.

Currently EC uses L = 512 and S = 8. This means that numbers are
packed in blocks of 512 bytes and gives space for even bigger
processor registers up to 512 bits.


Conclusions
-----------

This method requires a single variable/processor register for each
bit. This can be challenging if we want to avoid additional memory
accesses, even if we use modern processors that have many registers.
However, the implementation we chose for the Vandermonde Matrix
doesn't require temporary storage, so we don't need a full set of 8
new registers (one for each bit) to store partial computations.
Additionally, the computation of the multiplications requires, at
most, 2 extra registers, but this is afordable.

Xors are a really fast operation in modern processors. Intel CPU's
can dispatch up to 3 xors per CPU cycle if there are no dependencies
with ongoing previous instructions. Worst case is 1 xor per cycle. So,
in some configurations, this method could be very near to the memory
speed.

Another interesting thing of this method is that all data it needs to
operate is packed in small sequential blocks of memory, meaning that
it can take advantage of the faster internal CPU caches.


Results
-------

For the particular case of 8 bits, EC can compute each multiplication
using 12.8 xors on average (without counting 0 and 1 that do not
require any xor). Some numbers require less, like 2 that only requires
3 xors.

Having all this, we can check some numbers to see the performance of
this method.

Maybe the most interesting thing is the average number of xors needed
to encode a single byte of data. To compute this we'll need to define
some variables:

  * K: Number of data fragments  
  * R: Number of redundancy fragments  
  * N: K + R  
  * B: Number of bits per number  
  * A: Average number of xors per number  
  * Z: Bits per CPU register (can be up to 256 for AVX registers)  
  * X: Average number of xors per CPU cycle  
  * L: Average cycles per load  
  * S: Average cycles per store  
  * G: Core speed in Hz  

_Total number of bytes processed for a single matrix multiplication_:

  * __Read__:    K * B * Z / 8  
  * __Written__: N * B * Z / 8  

_Total number of memory accesses_:

  * __Loads__:  K * B * N  
  * __Stores__: B * N  

>  We need to read the same K * B * Z bits, in registers of Z bits, N
>  times, one for each row of the matrix. However the last N - 1 reads
>  could be made from the internal CPU caches if conditions are good.

_Total number of operations_:

  * __Additions__:       (K - 1) * N  
  * __Multiplications__: K * N  

__Total number of xors__: B * (K - 1) * N + A * K * N =
                          N * ((A + B) * K - B)

__Xors per byte__: 8 * N * ((A + B) * K - B) / (K * B * Z)

__CPU cycles per byte__: 8 * N * ((A + B) * K - B) / (K * B * Z * X) +
                         8 * L * N / Z +      (loads)
                         8 * S * N / (K * Z)  (stores)

__Bytes per second__: G / {CPU cycles per byte}

Some xors per byte numbers for specific configurations (B=8):

              Z=64  Z=128  Z=256
    K=2/R=1   0.79  0.39   0.20
    K=4/R=2   1.76  0.88   0.44
    K=4/R=3   2.06  1.03   0.51
    K=8/R=3   3.40  1.70   0.85
    K=8/R=4   3.71  1.86   0.93
    K=16/R=4  6.34  3.17   1.59



[1]: https://en.wikipedia.org/wiki/Erasure_code
[2]: https://en.wikipedia.org/wiki/Reed%E2%80%93Solomon_error_correction
[3]: https://en.wikipedia.org/wiki/Systematic_code
[4]: https://en.wikipedia.org/wiki/Identity_matrix
[5]: https://en.wikipedia.org/wiki/Linear_independence
[6]: https://en.wikipedia.org/wiki/Vandermonde_matrix
[7]: https://en.wikipedia.org/wiki/Finite_field
[8]: https://en.wikipedia.org/wiki/Finite_field_arithmetic#Implementation_tricks
