from pomagma import sequents
from pomagma import parser
import glob

RULE_SETS = map(parser.parse, glob.glob('*.rules'))


def _test_contrapositives(rules):
    print '# contrapositives'
    print
    for rule in rules:
        print rule.ascii()
        print
        if len(rule.succedents) != 1:
            print '    TODO'
            print
            continue
        for seq in sequents.get_contrapositives(rule):
            print seq.ascii(indent=4)
            print


def test_contrapositives():
    for rules in RULE_SETS:
        yield _test_contrapositives, rules


def _test_normalize(rules):
    print '# normalized'
    print
    for rule in rules:
        print rule.ascii()
        print
        if len(rule.succedents) != 1:
            print '    TODO'
            print
            continue
        for seq in sequents.normalize(rule):
            print seq.ascii(indent=4)
            print


def test_normalize():
    for rules in RULE_SETS:
        yield _test_normalize, rules