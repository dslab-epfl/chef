#!/usr/bin/env python3

# Parses YAML files that describe the commands to be run in parallel.
# If run directly, it will output the commands + arguments that can be passed to
# GNU parallel.

import yaml  # requires the PyYAML package
import sys
import os
import subprocess


class Batch:
    class YAML:
        def __init__(self, path: str):
            f = open(path, 'r')
            fc = f.read()
            f.close()
            self.tree = yaml.safe_load(fc)


    class Command:
        def __init__(self, token: dict, variables: dict):
            self.line = token['line'].split()
            self.variables = self.filter(variables)
            self.config = token['config']

        def filter(self, variables: {str: str}):
            used_variables = {}
            for k, vs in variables.items():
                if '{%s}' % k in self.line:
                    used_variables[k] = vs
            return used_variables

        def substitute(self, line: [str], key: str, values: [str]):
            lines = []
            for v in values:
                l = [w.replace('{%s}' % key, v) for w in line]
                lines.append(l)
            return lines

        def get_cmd_lines(self):
            cmd_lines = [self.line]
            for k, vs in self.variables.items():
                cmd_lines_new = []
                for c in cmd_lines:
                    cmd_lines_new.extend(self.substitute(c, k, vs))
                cmd_lines = cmd_lines_new
            return cmd_lines


    def __init__(self, path_yaml: str):
        if not os.path.isfile(path_yaml):
            raise Exception("%s: File not found" % path_yaml)
        yaml = Batch.YAML(path_yaml)
        self.variables = yaml.tree['variables']
        self.commands = []
        for ctoken, i in zip(yaml.tree['commands'], range(len(yaml.tree['commands']))):
            c = Batch.Command(ctoken, self.variables)
            self.commands.append(c)

    def get_cmd_lines(self):
        cmd_lines = []
        for c in self.commands:
            cmd_lines.extend(c.get_cmd_lines())
        return cmd_lines

    def get_commands(self):
        return self.commands


if __name__ == '__main__':
    if len(sys.argv) < 2:
        print("Usage: %s YAML [OUTDIR]" % sys.argv[0], file=sys.stderr)
        exit(1)

    path_yaml = sys.argv[1]
    path_results = 'batch_results' if len(sys.argv) < 3 else sys.argv[2]

    b = Batch(path_yaml, path_results)
    cmd_lines = b.get_cmd_lines()
    for c in cmd_lines:
        print(c)
