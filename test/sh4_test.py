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
parser.add_argument('-as', help='sh-elf-as path.', required=True)
parser.add_argument('-ld', help='sh-elf-ld path.', required=True)
parser.add_argument('-nm', help='sh-elf-nm path.', required=True)
parser.add_argument('-objcopy', help='sh-elf-objcopy path.', required=True)
parser.add_argument('-o', help='Specifies the output file.', required=True)
args = parser.parse_args()

def compile_asm(input_path):
  obj_file = tempfile.mktemp(suffix='.obj')
  srec_file = tempfile.mktemp(suffix='.srec')
  bin_file = tempfile.mktemp(suffix='.bin')
  subprocess.call([vars(args)['as'], '-little', '-o', obj_file, input_path])
  subprocess.call([vars(args)['ld'], '--oformat', 'srec', '-Ttext', '0x8c010000', '-e', '0x8c010000', '-o', srec_file, obj_file])
  map = subprocess.check_output([vars(args)['nm'], obj_file])
  subprocess.call([vars(args)['objcopy'], '-I', 'srec', '-O', 'binary', '-R', '.sec1', srec_file, bin_file])
  with open(bin_file, 'r') as f:
    bin = f.read()
  return (map, bin)

def parse_symbol_map(map, bin):
  tests = {}
  symbols = re.findall('([\d\w]+)\s+t\s+(test_.+)', map)
  for i, symbol in enumerate(symbols):
    offset = int(symbol[0], 16)
    name = symbol[1]
    tests[name] = { 'offset': offset, 'bin': bin, 'register_in': [], 'register_out': [] }
  return tests

def asm_to_tests(input_path):
  map, bin = compile_asm(input_path)

  # generate a test for each symbol in the map
  tests = parse_symbol_map(map, bin)

  # parse input, generating input / output registers for each test
  with open(input_path, 'r') as f:
    lines = f.readlines()

  current_test = None

  for line in lines:
    m = re.match('(test_.+?):', line)
    if m:
      current_test = m.group(1)
      continue
    m = re.match('\s+# REGISTER_IN\s+([^\s]+)\s+(.+)', line)
    if m:
      test = tests[current_test]
      test['register_in'].append((m.group(1), m.group(2)))
      continue
    m = re.match('\s+# REGISTER_OUT\s+([^\s]+)\s+(.+)', line)
    if m:
      test = tests[current_test]
      test['register_out'].append((m.group(1), m.group(2)))
      continue

  return tests

def test_to_struct(test):
  output = 'SH4Test {'
  output += hex(test['offset']) + ', '
  output += '(uint8_t *)"' + ''.join('\\x' + x.encode('hex') for x in test['bin']) + '", '
  output += str(len(test['bin'])) + ', '
  output += '{'
  for i, val in enumerate(test['register_in']):
    output += '{ SH4CTX_' + val[0].upper() + ', ' + val[1] + ' }'
    if (i != len(test['register_in']) - 1):
      output += ', '
  output += '}, '
  output += '{'
  for i, val in enumerate(test['register_out']):
    output += '{ SH4CTX_' + val[0].upper() + ', ' + val[1] + ' }'
    if (i != len(test['register_out']) - 1):
      output += ', '
  output += '}'
  output += '}'
  return output

def list_to_inc(inputs, output_path):
  with open(output_path, 'w') as f:
    for input in inputs:
      tests = asm_to_tests(input)
      for name in tests:
        struct = test_to_struct(tests[name])
        f.write('SH4_TEST(' + name + ', ' + struct + ')\n')

if __name__ == '__main__':
  list_to_inc(args.input, args.o)
