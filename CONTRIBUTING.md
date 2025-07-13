# Contributing to LizardNova

Thank you for considering contributing to LizardNova! We welcome pull requests and issues from the community.

## Development Setup

This project is built with [ESP-IDF](https://github.com/espressif/esp-idf) v5.x. Before contributing, make sure your environment is configured as described in the README:

```bash
. $IDF_PATH/export.sh
```

Run the following to build and execute the unit tests:

```bash
idf.py build
idf.py test
```

## Coding Style

* Use **4 spaces** for indentation.
* Place opening braces on a new line for functions.
* Keep line lengths under **100 characters**.
* Use descriptive names and add comments where necessary.

## Contribution Process

1. Fork this repository and create a feature branch.
2. Make your changes following the style guidelines above.
3. Ensure `idf.py build` and `idf.py test` run without errors.
4. Submit a pull request describing your changes.

We appreciate your contributions!
