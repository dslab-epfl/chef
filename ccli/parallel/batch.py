#!/usr/bin/env python3

# Parses YAML files that describe the commands to be run in parallel.
# If run directly, it will output the commands + arguments that can be passed to
# GNU parallel.

import yaml  # requires the PyYAML package
import sys
import os
import subprocess


PATH_RESULTS = 'results'

class Batch:
    class YAML:
        def __init__(self, path: str):
            f = open(path, 'r')
            fc = f.read()
            f.close()
            self.tree = yaml.safe_load(fc)


    class Command:
        def __init__(self, token: dict):
            self.line = token['line'].split()
            self.config = token['config']

        def filter(self, variables: dict):
            used_variables = {}
            for k, v in variables.items():
                if '{%s}' % k in self.line:
                    used_variables[k] = v
            #return '%s ::: %s' % (self.line, ' ::: '.join(used_variables))
            return used_variables

        def resolve(self, variables: dict):
            resolved = []
            for k, v in variables.items():
                resolved.append([k] + v)
            return resolved

        def flatten(self, variables: list):
            flattened = []
            for v in variables:
                flattened += [':::'] + v
            return flattened

        def get_cmd_line(self, prefix: [str], path_results: str, variables: dict):
            if not os.path.isdir(path_results):
                os.mkdir(path_results)

            parallel = ['parallel',
                        '--header', ':',
                        '--ungroup',
                        '--delay', '1',
                        '--results', path_results,
                        '--joblog', '%s/log' % path_results]
            line = self.line
            args = self.flatten(self.resolve(self.filter(variables)))
            return parallel + prefix + line + args


    def __init__(self, path: str):
        if not os.path.isfile(path):
            print('%s: File not found' % path, file=sys.stderr)
            exit(1)

        yaml = Batch.YAML(path)
        self.commands = []
        self.variables = yaml.tree['variables']
        for ctoken in yaml.tree['commands']:
            self.commands.append(Batch.Command(ctoken))

    def get_cmd_lines(self, prefix: [str], path_results: str):
        cmd_lines = []
        for c, i in zip(self.commands, range(len(self.commands))):
            cl = c.get_cmd_line(prefix, '%s/exp%04d' % (path_results, i), self.variables)
            cmd_lines.append(cl)
        return cmd_lines

    def run(self, prefix: [str], path_results: str):
        if os.path.isdir(path_results):
            print("%s: directory already exists" % path_results, file=sys.stderr)
            exit(1)
        else:
            os.mkdir(path_results)

        cmd_lines = self.get_cmd_lines(prefix, path_results)
        for c in cmd_lines:
            with open(os.devnull, 'w') as fp:
                subprocess.call(c, stderr=fp, stdout=fp)


if __name__ == '__main__':
    if len(sys.argv) < 2:
        print("Usage: %s YAML [OUTDIR]" % sys.argv[0], file=sys.stderr)
        exit(1)

    path_yaml = sys.argv[1]
    path_results = PATH_RESULTS if len(sys.argv) < 3 else sys.argv[2]

    b = Batch(path_yaml)
    b.run([], path_results)
    print("The results have been logged in %s" % path_results)
