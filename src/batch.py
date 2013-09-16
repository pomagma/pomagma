import os
import sys
import time
import glob
import shutil
import itertools
import subprocess
import parsable
parsable = parsable.Parsable()
import pomagma.util
import contextlib
from pomagma import surveyor, cartographer, theorist, analyst, atlas


DEFAULT_SURVEY_SIZE = 16384 + 512 - 1
MIN_SLEEP_SEC = 1
MAX_SLEEP_SEC = 600
PYTHON = sys.executable


class Sleeper(object):
    def __init__(self, name):
        self.name = name
        self.duration = MIN_SLEEP_SEC

    def reset(self):
        self.duration = MIN_SLEEP_SEC

    def sleep(self):
        sys.stderr.write('# {} sleeping\n'.format(self.name))
        sys.stderr.flush()
        time.sleep(self.duration)
        self.duration = min(MAX_SLEEP_SEC, 2 * self.duration)


class parsable_fork:
    def __init__(self, fun, *args, **kwargs):
        self.args = [PYTHON, '-m', 'pomagma.batch', fun.__name__]
        self.args += map(str, args)
        for key, val in kwargs.iteritems():
            self.args.append('{}={}'.format(key, val))
        self.proc = subprocess.Popen(self.args)

    def wait(self):
        self.proc.wait()
        code = self.proc.returncode
        assert code == 0, '\n'.join([
            'forked command failed with exit code {}'.format(code),
            ' '.join(self.args)])

    def terminate(self):
        if self.proc.poll() is None:
            self.proc.terminate()


@contextlib.contextmanager
def in_atlas(theory, init=False):
    path = os.path.join(pomagma.util.DATA, 'atlas', theory)
    if init:
        assert not os.path.exists(path), 'World map is already initialized'
        os.makedirs(path)
    else:
        assert os.path.exists(path), 'First initialize world map'
    with pomagma.util.chdir(path):
        yield


@parsable.command
def test(theory, **options):
    '''
    Test basic operations in one theory:
        init, validate, copy, survey, aggregate,
        conjecture_diverge, conjecture_equal
    Options: log_level, log_file
    '''
    buildtype = 'debug' if pomagma.util.debug else 'release'
    path = os.path.join(pomagma.util.DATA, 'test', buildtype, 'atlas', theory)
    if os.path.exists(path):
        os.system('rm -f {}/*'.format(path))
    else:
        os.makedirs(path)
    with pomagma.util.chdir(path), pomagma.util.mutex(block=False):

        min_size = pomagma.util.MIN_SIZES[theory]
        dsize = min(64, 1 + min_size)
        sizes = [min_size + i * dsize for i in range(10)]
        opts = options
        opts.setdefault('log_file', 'test.log')
        theorems = 'theorems.facts'
        conjectures = 'conjectures.facts'
        simplified = 'simplified.facts'

        surveyor.init(theory, '0.h5', sizes[0], **opts)
        with cartographer.load(theory, '0.h5', **opts) as db:
            db.validate()
            db.dump('1.h5')
        surveyor.survey(theory, '1.h5', '2.h5', sizes[1], **opts)
        with cartographer.load(theory, '2.h5', **opts) as db:
            db.trim(sizes[0], ['3.h5'])
        surveyor.survey(theory, '3.h5', '4.h5', sizes[1], **opts)
        with cartographer.load(theory, '2.h5', **opts) as db:
            db.aggregate('4.h5')
            db.dump('5.h5')
        with cartographer.load(theory, '5.h5', **opts) as db:
            db.aggregate('0.h5')
            db.dump('6.h5')
        digest5 = pomagma.util.get_hash('5.h5')
        digest6 = pomagma.util.get_hash('6.h5')
        assert digest5 == digest6

        theorist.conjecture_diverge(theory, '6.h5', conjectures, **opts)
        if theory != 'h4':
            theorist.try_prove_diverge(
                conjectures,
                conjectures,
                theorems,
                **opts)
            with cartographer.load(theory, '6.h5', **opts) as db:
                db.assume(theorems)
                db.validate()
        theorist.conjecture_equal(theory, '6.h5', conjectures, **opts)
        theorist.try_prove_nless(
            theory,
            '6.h5',
            conjectures,
            conjectures,
            theorems,
            **opts)
        if theory != 'h4':
            with cartographer.load(theory, '6.h5', **opts) as db:
                db.assume(theorems)
                db.validate()
                for priority in [0, 1]:
                    while db.infer(priority):
                        db.validate()
                for priority in [0, 1]:
                    assert not db.infer(priority)

        analyst.simplify(theory, '6.h5', conjectures, simplified, **opts)


@parsable.command
def init(theory, **options):
    '''
    Initialize world map for given theory.
    Options: log_level, log_file
    '''
    with in_atlas(theory, init=True):
        world = 'world.h5'
        survey = pomagma.util.temp_name('survey.h5')
        opts = options
        log_file = opts.setdefault('log_file', 'init.log')
        world_size = pomagma.util.MIN_SIZES[theory]
        pomagma.util.log_print('initialize to {}'.format(world_size), log_file)
        surveyor.init(theory, survey, world_size, **opts)
        atlas.initialize(theory, world, survey, **opts)


class StructureQueue(object):
    def __init__(self, path):
        self.path = path

    def get(self):
        # specifically ignore temporary files like temp.1234.0.h5
        return glob.glob(os.path.join(self.path, '[0-9]*.h5'))

    def __iter__(self):
        return iter(self.get())

    def __len__(self):
        return len(self.get())

    def try_pop(self, destin):
        for source in self:
            os.rename(source, destin)
            return True
        return False

    def push(self, source):
        if self.path and not os.path.exists(self.path):
            os.makedirs(self.path)
        with pomagma.util.mutex(self.path):
            for i in itertools.count():
                destin = os.path.join(self.path, '{}.h5'.format(i))
                if not os.path.exists(destin):
                    os.rename(source, destin)
                    return

    def clear(self):
        for item in self:
            os.remove(item)


class CartographerWorker(object):
    def __init__(self, theory, region_size, region_queue_size, **options):
        self.log_file = options['log_file']
        self.world = 'world.h5'
        self.normal_world = 'world.normal.h5'
        self.region_size = region_size
        self.region_queue = StructureQueue('region.queue')
        self.survey_queue = StructureQueue('survey.queue')
        self.region_queue_size = region_queue_size
        self.server = cartographer.serve(theory, self.world, **options)
        self.db = self.server.connect()
        self.infer_state = 0

    def stop(self):
        self.server.stop()

    def is_normal(self):
        assert self.infer_state in [0, 1, 2]
        return self.infer_state == 2

    def try_work(self):
        return (
            self.try_trim() or
            self.try_normalize() or
            self.try_aggregate()
        )

    def try_trim(self):
        queue_size = len(self.region_queue)
        if queue_size >= self.region_queue_size:
            return False
        else:
            self.fill_region_queue(self.region_queue)
            return True

    def try_normalize(self):
        if self.is_normal():
            return False
        else:
            if self.db.infer(self.infer_state):
                self.db.validate()
                self.db.dump(self.world)
                self.replace_region_queue()
            else:
                self.infer_state += 1
                if self.is_normal():
                    self.db.dump(self.normal_world)
            return True

    def try_aggregate(self):
        surveys = self.survey_queue.get()
        if not surveys:
            return False
        else:
            for survey in surveys:
                self.db.aggregate(survey)
                self.infer_state = 0
                self.db.validate()
                self.db.dump(self.world)
                world_size = pomagma.util.get_item_count(self.world)
                pomagma.util.log_print(
                    'world_size = {}'.format(world_size),
                    self.log_file)
                os.remove(survey)
            self.replace_region_queue()
            return True

    def fill_region_queue(self, queue):
        if not os.path.exists(queue.path):
            os.makedirs(queue.path)
        queue_size = len(queue)
        trim_count = max(0, self.region_queue_size - queue_size)
        regions_out = []
        for i in itertools.count():
            region_out = os.path.join(queue.path, '{}.h5'.format(i))
            if not os.path.exists(region_out):
                regions_out.append(region_out)
                if len(regions_out) == trim_count:
                    break
        self.db.trim(self.region_size, regions_out)

    def replace_region_queue(self):
        temp_path = pomagma.util.temp_name(self.region_queue.path)
        if os.path.exists(temp_path):
            shutil.rmtree(temp_path)
        self.fill_region_queue(StructureQueue(temp_path))
        self.region_queue.clear()
        os.rename(temp_path, self.region_queue.path)


@parsable.command
def cartographer_work(
        theory,
        region_size=(DEFAULT_SURVEY_SIZE - 512),
        region_queue_size=4,
        **options):
    '''
    Start cartographer worker.
    '''
    min_size = pomagma.util.MIN_SIZES[theory]
    assert region_size >= min_size
    with in_atlas(theory):
        opts = options
        opts.setdefault('log_file', 'cartographer.log')
        worker = CartographerWorker(
            theory,
            region_size,
            region_queue_size,
            **options)
        try:
            sleeper = Sleeper('cartographer')
            while True:
                if not worker.try_work():
                    sleeper.sleep()
                else:
                    sleeper.reset()
        finally:
            worker.stop()


@parsable.command
def survey_work(theory, step_size=512, **options):
    '''
    Start survey worker.
    '''
    assert step_size > 0
    with in_atlas(theory):
        region_queue = StructureQueue('region.queue')
        survey_queue = StructureQueue('survey.queue')
        region = pomagma.util.temp_name('region.h5')
        survey = pomagma.util.temp_name('survey.h5')
        opts = options
        opts.setdefault('log_file', 'survey.log')
        sleeper = Sleeper('surveyor')
        while True:
            if not region_queue.try_pop(region):
                sleeper.sleep()
            else:
                sleeper.reset()
                region_size = pomagma.util.get_item_count(region)
                survey_size = region_size + step_size
                surveyor.survey(theory, region, survey, survey_size, **opts)
                os.remove(region)
                survey_queue.push(survey)


@parsable.command
def explore(
        theory,
        max_size=DEFAULT_SURVEY_SIZE,
        step_size=512,
        region_queue_size=4,
        **options):
    '''
    Continuously expand world map for given theory, inferring and surveying.
    Options: log_level, log_file
    '''
    assert step_size > 0
    region_size = max_size - step_size
    min_size = pomagma.util.MIN_SIZES[theory]
    assert region_size >= min_size
    region_size = max(min_size, max_size - step_size)
    workers = [
        parsable_fork(
            cartographer_work,
            theory,
            region_size,
            region_queue_size,
            **options),
        parsable_fork(survey_work, theory, step_size, **options),
        # TODO add theorist_work
    ]
    try:
        for worker in workers:
            worker.wait()
    finally:
        for worker in workers:
            worker.terminate()


@parsable.command
def translate(theory, **options):
    '''
    Translate language of world map (e.g. when language changes).
    Options: log_level, log_file
    '''
    with in_atlas(theory):
        world = 'world.h5'
        init = pomagma.util.temp_name('init.h5')
        aggregate = pomagma.util.temp_name('aggregate.h5')
        assert os.path.exists(world), 'First initialize world map'
        opts = options
        opts.setdefault('log_file', 'translate.log')
        init_size = pomagma.util.MIN_SIZES[theory]
        surveyor.init(theory, init, init_size, **opts)
        atlas.translate(theory, init, world, aggregate, **opts)


@parsable.command
def theorize(theory, **options):
    '''
    Make conjectures based on atlas and update atlas based on theorems.
    '''
    with in_atlas(theory):
        world = 'world.h5'
        updated = pomagma.util.temp_name('assume.h5')
        temp_conjectures = pomagma.util.temp_name('conjectures.facts')
        diverge_conjectures = 'diverge_conjectures.facts'
        diverge_theorems = 'diverge_theorems.facts'
        equal_conjectures = 'equal_conjectures.facts'
        nless_theorems = 'nless_theorems.facts'
        assert os.path.exists(world), 'First build world map'
        opts = options
        opts.setdefault('log_file', 'theorize.log')

        theorist.conjecture_diverge(theory, world, diverge_conjectures, **opts)
        theorem_count = theorist.try_prove_diverge(
            diverge_conjectures,
            temp_conjectures,
            diverge_theorems,
            **opts)
        os.rename(temp_conjectures, diverge_conjectures)
        if theorem_count > 0:
            atlas.assume(theory, world, updated, diverge_theorems, **opts)

        theorem_count = theorist.try_prove_nless(
            theory,
            world,
            equal_conjectures,
            temp_conjectures,
            nless_theorems,
            **opts)
        os.rename(temp_conjectures, equal_conjectures)
        if theorem_count > 0:
            atlas.assume(theory, world, updated, nless_theorems, **opts)


@parsable.command
def profile(theory, size_blocks=3, dsize_blocks=0, **options):
    '''
    Profile surveyor through callgrind on random region of world.
    Inputs: theory, region size in blocks (1 block = 512 obs)
    Options: log_level, log_file
    '''
    size = size_blocks * 512 - 1
    dsize = dsize_blocks * 512
    min_size = pomagma.util.MIN_SIZES[theory]
    assert size >= min_size
    with in_atlas(theory):
        opts = options
        opts.setdefault('log_file', 'profile.log')
        region = 'region.{:d}.h5'.format(size)
        temp = pomagma.util.temp_name('profile.h5')
        world = 'world.h5'

        if not os.path.exists(region):
            assert os.path.exists(world), 'First initialize world map'
            cartographer.trim(theory, world, region, size, **opts)
        opts.setdefault('runner', 'valgrind --tool=callgrind')
        surveyor.survey(theory, region, temp, size + dsize, **opts)


def sparse_range(min_size, max_size):
    sizes = [512]
    while True:
        size = 2 * sizes[-1]
        if size <= max_size:
            sizes.append(size)
        else:
            break
    sizes += [3 * s for s in sizes if 3 * s < max_size]
    sizes = [s - 1 for s in sizes if s > min_size]
    sizes.sort()
    return sizes


@parsable.command
def trim_regions(theory, **options):
    '''
    Trim a set of regions for testing on small machines.
    '''
    with in_atlas(theory):
        opts = options
        opts.setdefault('log_file', 'trim_regions.log')
        world = 'world.h5'
        min_size = pomagma.util.MIN_SIZES[theory]
        max_size = pomagma.util.get_item_count(world)
        sizes = reversed(sparse_range(min_size, max_size))
        larger = world
        for size in sizes:
            print 'Trimming region of size', size
            smaller = 'region.{:d}.h5'.format(size)
            cartographer.trim(theory, larger, smaller, size, **opts)
            larger = smaller
        cartographer.validate(larger, **opts)


@parsable.command
def make(theory, max_size=8191, step_size=512, **options):
    '''
    Initialize; explore.
    '''
    path = os.path.join(pomagma.util.DATA, 'atlas', theory)
    if not os.path.exists(path):
        init(theory, **options)
    explore(theory, max_size, step_size, **options)


@parsable.command
def clean(theory):
    '''
    Remove all work for given theory. DANGER
    '''
    print 'Clearing {} Are you sure? [y/N]'.format(theory),
    if raw_input().lower()[:1] == 'y':
        print 'OK, clearing.'
        path = os.path.join(pomagma.util.DATA, 'atlas', theory)
        if os.path.exists(path):
            shutil.rmtree(path)
    else:
        print 'OK, not clearing.'


if __name__ == '__main__':
    parsable.dispatch()
