import re
from pomagma.util import TODO, union, set_with, set_without, inputs
from pomagma.expressions import Expression, as_variable


class Sequent(object):
    def __init__(self, antecedents, succedents):
        antecedents = frozenset(antecedents)
        succedents = frozenset(succedents)
        for expr in antecedents | succedents:
            assert isinstance(expr, Expression)
        self._antecedents = antecedents
        self._succedents = succedents
        self._hash = hash((antecedents, succedents))
        self.debuginfo = {}

    @property
    def antecedents(self):
        return self._antecedents

    @property
    def succedents(self):
        return self._succedents

    def __hash__(self):
        return self._hash

    def __eq__(self, other):
        return (self._antecedents == other._antecedents and
                self._succedents == other._succedents)

    def __str__(self):
        return '{0} |- {1}'.format(
            ', '.join(map(str, self.antecedents)),
            ', '.join(map(str, self.succedents)))

    def ascii(self, indent = 0):
        top = '   '.join(map(str, self.antecedents))
        bot = '   '.join(map(str, self.succedents))
        bar = '-' * max(len(top), len(bot))
        diff = len(top) - len(bot)
        lpad = abs(diff) / 2
        rpad = diff - lpad
        if diff > 0 and bot:
            bot = ' ' * lpad + bot + ' ' * rpad
        if diff < 0 and top:
            top = ' ' * lpad + top + ' ' * rpad
        lines = [top, bar, bot]
        lines = filter(bool, lines)
        lines = map((' ' * indent).__add__, lines)
        return '\n'.join(lines)


@inputs(Expression)
def as_atom(expr):
    args = map(as_variable, expr.args)
    return Expression(expr.name, *args)


@inputs(Expression)
def get_negated(expr):
    'Returns a disjunction'
    if expr.name == 'LESS':
        return set([Expression('NLESS', *expr.args)])
    elif expr.name == 'NLESS':
        return set([Expression('LESS', *expr.args)])
    elif expr.name == 'EQUAL':
        lhs, rhs = expr.args
        return set([Expression('NLESS', lhs, rhs),
                    Expression('NLESS', rhs, lhs)])
    else:
        raise ValueError('expr cannot be negated: {}'.format(expr))


@inputs(Expression)
def as_antecedents(expr, bound):
    antecedents = set()
    if expr.arity != 'Variable':
        atom = as_atom(expr)
        if not (atom.varname in bound and
                all(arg.name in bound for arg in atom.args)):
            antecedents.add(atom)
        for arg in expr.args:
            antecedents |= as_antecedents(arg, bound)
    return antecedents


@inputs(Expression)
def as_succedent(expr, bound):
    antecedents = set()
    if expr.arity == 'Equation':
        args = []
        for arg in expr.args:
            if arg.varname in bound:
                args.append(as_variable(arg))
            else:
                args.append(as_atom(arg))
                for argarg in arg.args:
                    antecedents |= as_antecedents(argarg, bound)
        succedent = Expression(expr.name, *args)
    else:
        assert expr.args, expr.args
        succedent = as_atom(expr)
        for arg in expr.args:
            antecedents |= as_antecedents(arg, bound)
    return antecedents, succedent


@inputs(Sequent)
def get_pointed(seq):
    '''
    Return a set of sequents each with a single succedent.
    '''
    if len(seq.succedents) == 1:
        return set([seq])
    if len(seq.succedents) > 1:
        result = set()
        for succedent in seq.succedents:
            remaining = set_without(seq.succedents, succedent)
            negated = union(map(get_negated, remaining))
            # FIXME get_negated is a disjunction; do not union it
            neg_neg = union(map(get_negated, seq.antecedents))
            if not (negated & neg_neg):
                antecedents = seq.antecedents | negated
                result.add(Sequent(antecedents, set([succedent])))
        return result
    else:
        TODO('allow empty succedents')


@inputs(Sequent)
def get_atomic(seq, bound=set()):
    '''
    Return a set of normal sequents.
    '''
    result = set()
    for pointed in get_pointed(seq):
        seq_succedent = iter(pointed.succedents).next()
        antecedents, succedent = as_succedent(seq_succedent, bound)
        for a in pointed.antecedents:
            antecedents |= as_antecedents(a, bound)
        result.add(Sequent(antecedents, set([succedent])))
    return result


@inputs(Sequent)
def get_contrapositives(seq):
    if len(seq.succedents) == 1:
        seq_succedent = iter(seq.succedents).next()
        result = set()
        for antecedent in seq.antecedents:
            antecedents = set_without(seq.antecedents, antecedent)
            succedents = get_negated(antecedent)
            for disjunct in get_negated(seq_succedent):
                if get_negated(disjunct) & antecedents:
                    pass # contradiction
                else:
                    result.add(Sequent(
                        set_with(antecedents, disjunct),
                        succedents))
        return result
    elif len(seq.succedents) > 1:
        TODO('allow multiple succedents')
    else:
        TODO('allow empty succedents')


@inputs(Sequent)
def normalize(seq, bound=set()):
    '''
    Return a set of normal sequents, closed under contrapositive.
    '''
    result = get_atomic(seq, bound)
    for contra in get_contrapositives(seq):
        result |= get_atomic(contra, bound)
    return result


@inputs(Sequent)
def assert_normal(seq):
    assert len(seq.succedents) == 1
    for expr in seq.antecedents:
        for arg in expr.args:
            assert arg.arity == 'Variable'
    for expr in seq.succedents:
        if expr.arity == 'Equation':
            for arg in expr.args:
                for argarg in arg.args:
                    assert arg.arity == 'Variable'
        else:
            for arg in expr.args:
                assert arg.arity == 'Variable'