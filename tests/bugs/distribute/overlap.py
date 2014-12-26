#!/usr/bin/python

import sys

def calculate_one (ov, nv):
    old_start = int(ov[18:26],16)
    old_end = int(ov[26:34],16)
    new_start = int(nv[18:26],16)
    new_end = int(nv[26:34],16)
    if (new_end < old_start) or (new_start > old_end):
        #print '%s, %s -> ZERO' % (ov, nv)
        return 0
    all_start = max(old_start,new_start)
    all_end = min(old_end,new_end)
    #print '%s, %s -> %08x' % (ov, nv, all_end - all_start + 1)
    return all_end - all_start + 1

def calculate_all (values):
    total = 0
    nv_index = len(values) / 2
    for old_val in values[:nv_index]:
        new_val = values[nv_index]
        nv_index += 1
        total += calculate_one(old_val,new_val)
    return total

"""
test1_vals = [
    '0x0000000000000000000000003fffffff',   # first quarter
    '0x0000000000000000400000007fffffff',   # second quarter
    '0x000000000000000080000000ffffffff',   # second half
    '0x00000000000000000000000055555554',   # first third
    '0x000000000000000055555555aaaaaaa9',   # second third
    '0x0000000000000000aaaaaaaaffffffff',   # last third
]

test2_vals = [
    '0x0000000000000000000000003fffffff',   # first quarter
    '0x0000000000000000400000007fffffff',   # second quarter
    '0x000000000000000080000000ffffffff',   # second half
    '0x00000000000000000000000055555554',   # first third
    # Next two are (incorrectly) swapped.
    '0x0000000000000000aaaaaaaaffffffff',   # last third
    '0x000000000000000055555555aaaaaaa9',   # second third
]

print '%08x' % calculate_one(test1_vals[0],test1_vals[3])
print '%08x' % calculate_one(test1_vals[1],test1_vals[4])
print '%08x' % calculate_one(test1_vals[2],test1_vals[5])
print '= %08x' % calculate_all(test1_vals)
print '%08x' % calculate_one(test2_vals[0],test2_vals[3])
print '%08x' % calculate_one(test2_vals[1],test2_vals[4])
print '%08x' % calculate_one(test2_vals[2],test2_vals[5])
print '= %08x' % calculate_all(test2_vals)
"""

if __name__ == '__main__':
    # Return decimal so bash can reason about it.
    print '%d' % calculate_all(sys.argv[1:])
