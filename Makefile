all:
	$(MAKE) static-check
	pip install -e .
	$(MAKE) -C src/language
	$(MAKE) -C src/cartographer
	$(MAKE) -C src/analyst
	POMAGMA_DEBUG=1 python -m pomagma.make build
	python -m pomagma.make build

static-check: FORCE
	find src | grep '.py$$' | grep -v '_pb2.py' | xargs pyflakes
	find src | grep '.py$$' | grep -v '_pb2.py' | xargs pep8

unit-test: all FORCE
	POMAGMA_DEBUG=1 python -m pomagma.make test-units -v
batch-test: all FORCE
	POMAGMA_DEBUG=1 python -m pomagma.make test-atlas
h4-test: all FORCE
	POMAGMA_DEBUG=1 python -m pomagma.make test-atlas h4
sk-test: all FORCE
	POMAGMA_DEBUG=1 python -m pomagma.make test-atlas sk
skj-test: all FORCE
	POMAGMA_DEBUG=1 python -m pomagma.make test-atlas skj
skrj-test: all FORCE
	POMAGMA_DEBUG=1 python -m pomagma.make test-atlas skrj
test: all FORCE
	POMAGMA_DEBUG=1 python -m pomagma.make test-units -v
	POMAGMA_DEBUG=1 python -m pomagma.make test-atlas

h4: all
	python -m pomagma make h4
sk: all
	python -m pomagma make sk
skj: all
	python -m pomagma make skj
skrj: all
	python -m pomagma make skrj

python-libs:
	@$(MAKE) -C src/language all

profile:
	python -m pomagma.make profile-util
	python -m pomagma.make profile-surveyor
	# TODO add profile for sequential & concurrent dense_set

clean: FORCE
	rm -rf build lib include
	git clean -fdx -e pomagma.egg-info -e node_modules -e data

FORCE:
