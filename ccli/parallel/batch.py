#!/usr/bin/env python3

# Parses YAML files that describe the commands to be run in parallel.
# If run directly, it will output the commands + arguments that can be passed to
# GNU parallel.

import yaml  # requires the PyYAML package
import sys
import os


class Batch:
    class YAML:
        def __init__(self, path: str):
            f = open(path, 'r')
            fc = f.read()
            f.close()
            self.tree = yaml.safe_load(fc)


    class Command:
        def __init__(self, token: dict):
            self.line = token['line']
            self.config = token['config']

        def resolve(self, variables: dict):
            used_variables = []
            for k, v in variables.items():
                if self.line.find(k) >= 0:
                    used_variables.append('%s %s' % (k, ' '.join(v)))

            lines = []
            return '%s ::: %s' % (self.line, ' ::: '.join(used_variables))


    def __init__(self, path: str):
        if not os.path.isfile(path):
            print('%s: File not found' % path, file=sys.stderr)
            exit(1)

        yaml = Batch.YAML(path)
        self.commands = []
        self.variables = yaml.tree['variables']
        for ctoken in yaml.tree['commands']:
            self.commands.append(Batch.Command(ctoken))

    def resolve(self):
        s = []
        for c in self.commands:
            s.append(c.resolve(self.variables))
        return s


def main():
    yaml_path = 'examples.yml' if len(sys.argv) == 1 else sys.argv[1]

    b = Batch(yaml_path)
    print('\n'.join(b.resolve()))


if __name__ == '__main__':
    main()
