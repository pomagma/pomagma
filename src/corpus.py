import re
from collections import deque
import os
import sqlite3
import parsable
parsable = parsable.Parsable()
import pomagma.util

DUMP = os.path.join(pomagma.util.SRC, 'corpus.dump')
STORE = os.path.join(pomagma.util.DATA, 'corpus.db')

IDENTIFIER_RE = re.compile(r'^[^\d\W]\w*(\.[^\d\W]\w*)*$')
KEYWORD_RE = re.compile(r'^[A-Z]+$')

SCHEMA = '''(
    id integer primary key,
    name text,
    code text,
    args text
)'''


def table_exists(conn, name):
    cursor = conn.execute(
        '''
        SELECT name FROM sqlite_master
        WHERE type='table' AND name=?
        ''',
        (name,))
    return cursor.fetchone() is not None


def load_line(line):
    assert isinstance(line, basestring)
    tokens = deque(line.split())
    head = tokens.popleft()
    assert head in ['LET', 'ASSERT'], line
    if head == 'LET':
        name = tokens.popleft()
        assert IDENTIFIER_RE.match(name), name
        assert not KEYWORD_RE.match(name), name
    elif head == 'ASSERT':
        name = ''
    args = []
    while tokens and not KEYWORD_RE.match(tokens[-1]):
        args.append(tokens.pop())
        if tokens:
            assert tokens.popleft() == 'APP', line
    if not tokens:
        tokens.append('I')
    args.reverse()
    args = ' '.join(args)
    assert len(set(args)) == len(args), line
    code = ' '.join(tokens)
    return name, code, args


def dump_line(name, code, args):
    assert isinstance(name, basestring), name
    assert isinstance(code, basestring), code
    assert isinstance(args, basestring), args
    if name:
        assert IDENTIFIER_RE.match(name), name
        assert not KEYWORD_RE.match(name), name
        parts = ['LET', name]
    else:
        parts = ['ASSERT']
    for _ in args.split():
        parts.append('APP')
    parts.append(code)
    parts.append(args)
    line = ' '.join(parts)
    return line


def nonblank_lines(fd):
    for line in fd:
        line = line.split('#', 1)[0].strip()
        if line:
            yield line
    raise StopIteration()


class Corpus:
    def __init__(self):
        dirname = os.path.dirname(STORE)
        if not os.path.exists(dirname):
            os.makedirs(dirname)
        self.conn = sqlite3.connect(STORE)
        if not table_exists(self.conn, 'lines'):
            self._init_tables()
            self.load()

    def close(self):
        self.conn.close()

    def _clear_tables(self):
        self.conn.execute('DELETE FROM lines')
        self.conn.commit()

    def _init_tables(self):
        self.conn.execute('CREATE TABLE lines {}'.format(SCHEMA))
        self.conn.commit()

    def load(self):
        print 'loading corpus'
        self._clear_tables()
        with open(DUMP, 'r') as f:
            self.conn.executemany(
                'INSERT INTO lines (name, code, args) VALUES (?, ?, ?)',
                (load_line(line) for line in nonblank_lines(f)))
        self.conn.commit()

    def dump(self):
        print 'dumping corpus'
        lines = []
        cursor = self.conn.execute('SELECT name, code, args FROM lines')
        for name, code, args in cursor:
            line = dump_line(name, code, args)
            lines.append(line)
        lines.sort()
        with open(DUMP, 'w') as f:
            f.write('# this file is managed by corpus.py\n')
            for line in lines:
                f.write(line)
                f.write('\n')

    def insert(self, name, code, args):
        self.conn.execute(
            'INSERT INTO lines (name, code, args) VALUES (?, ?, ?)',
            (name, code, args))

    def update(self, id, name, code, args):
        self.conn.execute(
            'UPDATE lines SET name=?, code=?, args=?  WHERE id=?',
            (name, code, args, id))
        self.conn.commit()

    def remove(self, id):
        self.conn.execute('DELETE FROM lines WHERE id=?', (id,))
        self.conn.commit()

    def find_by_id(self, id):
        cursor = self.conn.execute(
            'SELECT * FROM lines WHERE id=?',
            (id,))
        return cursor.findone()

    def find_by_name(self, name):
        cursor = self.conn.execute(
            'SELECT * FROM lines WHERE name=?',
            (name,))
        return cursor.findone()

    def find_all(self):
        return self.conn.execute('SELECT * FROM lines')


@parsable.command
def load():
    '''
    Load local database from dump in git working tree.
    '''
    assert not os.path.exists(STORE)
    corpus = Corpus()
    corpus.load()
    corpus.close()


@parsable.command
def dump():
    '''
    Dump local database to git working tree.
    '''
    assert os.path.exists(STORE)
    corpus = Corpus()
    corpus.dump()
    corpus.close()


@parsable.command
def clear():
    '''
    Removes local database.
    '''
    assert os.path.exists(STORE)
    os.remove(STORE)


if __name__ == '__main__':
    parsable.dispatch()
