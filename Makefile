.PHONY: test native-test native-test-107 migration-test api-check fixtures

test: native-test native-test-107 migration-test api-check

native-test:
	$(MAKE) -C test test

native-test-107:
	$(MAKE) -C test test-107

migration-test:
	python -m unittest discover -s tools -p "test_*.py"

api-check:
	python scripts/check_api.py

fixtures:
	python tools/generate_fixtures.py
