#!/usr/bin/env python

import argparse
import os.path
import re
import string
import subprocess
import sys
import tempfile

parser = argparse.ArgumentParser(description='Process an assembly source file and output it as an ASMTest constructor invocation.')
parser.add_argument('input', nargs='+', help='Specifies the input file(s).')
parser.add_argument('-o', '--output', help='Specifies the output file.', required=True)
args = parser.parse_args()

def asm_to_bin(input_path):
  obj = tempfile.mktemp(suffix='.obj')
  srec = tempfile.mktemp(suffix='.srec')
  bin = tempfile.mktemp(suffix='.bin')
  subprocess.call(['sh-elf-as', '-little', '-o', obj, input_path])
  subprocess.call(['sh-elf-ld', '--oformat', 'srec', '-Ttext', '0x8c010000', '-o', srec, obj])
  subprocess.call(['sh-elf-objcopy', '-I', 'srec', '-O', 'binary', '-R', '.sec1', srec, bin])
  with open(bin, 'r') as f:
    return f.read()

def asm_to_test(input_path):
  # parse input
  with open(input_path, 'r') as f:
    input_str = f.read()

  binary = asm_to_bin(input_path)
  register_in = re.findall('# REGISTER_IN\s+([^\s]+)\s+(.+)', input_str)
  register_out = re.findall('# REGISTER_OUT\s+([^\s]+)\s+(.+)', input_str)

  # generate output
  output = 'SH4Test {'
  output += '(uint8_t *)"' + ''.join('\\x' + x.encode('hex') for x in binary) + '", '
  output += str(len(binary)) + ', '
  output += '{'
  for i, entry in enumerate(register_in):
    output += '{ SH4CTX_' + entry[0].upper() + ', ' + entry[1] + ' }'
    if (i != len(register_in) - 1):
      output += ', '
  output += '}, '
  output += '{'
  for i, entry in enumerate(register_out):
    output += '{ SH4CTX_' + entry[0].upper() + ', ' + entry[1] + ' }'
    if (i != len(register_out) - 1):
      output += ', '
  output += '}'
  output += '}'

  return output

def list_to_inc(inputs, output_path):
  with open(output_path, 'w') as f:
    for input in inputs:
      test_name = os.path.splitext(os.path.basename(input))[0]
      test = asm_to_test(input)
      f.write('SH4_TEST(' + test_name + ', ' + test + ')\n')

if __name__ == '__main__':
  list_to_inc(args.input, args.output)
