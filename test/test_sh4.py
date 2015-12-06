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

def parse_value(value):
  if value.startswith('0x'):
    return int(value, 16);
  return int(value)

def add_value(test, set, idx, value):
  if idx == None:
    test[set] = value
  elif set.startswith('dr') or set.startswith('xd'):
    # convert 64-bit dr / xd register values into the pairs of fr and xf
    # registers that they alias
    set = set.replace('dr', 'fr')
    set = set.replace('xd', 'xf')
    test[set][idx] = (value & 0xffffffff);
    test[set][idx + 1] = ((value >> 32) & 0xffffffff);
  else:
    test[set][idx] = value

def format_values(values):
  if (type(values) is list):
    output = ''
    for v in values:
      output += hex(v & 0xffffffff) + ','
    output = output.strip(',')
  else:
    output = hex(values & 0xffffffff)
  return output

def format_test(test):
  output = 'TEST_SH4('
  output += test['name'] + ','
  output += '(uint8_t *)"' + ''.join('\\x' + x.encode('hex') for x in test['bin']) + '",'
  output += str(len(test['bin'])) + ','
  output += hex(test['offset']) + ','
  output += format_values(test['fpscr_in']) + ','
  output += format_values(test['r_in']) + ','
  output += format_values(test['fr_in']) + ','
  output += format_values(test['xf_in']) + ','
  output += format_values(test['fpscr_out']) + ','
  output += format_values(test['r_out']) + ','
  output += format_values(test['fr_out']) + ','
  output += format_values(test['xf_out'])
  output += ')'
  return output

def new_test(name, bin, offset):
  return {
    'name': name,
    'bin': bin,
    'offset': offset,
    'fpscr_in': 0xbaadf00d,
    'r_in': [0xbaadf00d] * 16,
    'fr_in': [0xbaadf00d] * 16,
    'xf_in': [0xbaadf00d] * 16,
    'fpscr_out': 0xbaadf00d,
    'r_out': [0xbaadf00d] * 16,
    'fr_out': [0xbaadf00d] * 16,
    'xf_out': [0xbaadf00d] * 16
  }

def parse_symbol_map(map, bin):
  tests = {}
  symbols = re.findall('([\d\w]+)\s+t\s+(test_.+)', map)
  for i, symbol in enumerate(symbols):
    offset = int(symbol[0], 16)
    name = symbol[1]
    tests[name] = new_test(name, bin, offset)
  return tests

def asm_to_tests(input_path):
  map, bin = compile_asm(input_path)

  # generate a test for each symbol in the map
  tests = parse_symbol_map(map, bin)

  # parse input, generating input / output registers for each test
  with open(input_path, 'r') as f:
    lines = f.readlines()

  # iterate source, using labels and comments as metadata describing
  # each test
  current_test = None

  for line in lines:
    m = re.match('(test_.+?):', line)
    if m:
      current_test = m.group(1)
      continue
    m = re.match('\s+# REGISTER_(IN|OUT)\s+(\w+?)(\d*?)\s+(.+)', line)
    if m:
      set = (m.group(2) + '_' + m.group(1)).lower()
      idx = int(m.group(3)) if m.group(3) else None
      value = parse_value(m.group(4))
      add_value(tests[current_test], set, idx, value)

  return tests

def list_to_inc(inputs, output_path):
  with open(output_path, 'w') as f:
    for input in inputs:
      tests = asm_to_tests(input)
      for name in tests:
        test = format_test(tests[name])
        f.write(test + '\n')

if __name__ == '__main__':
  list_to_inc(args.input, args.o)
