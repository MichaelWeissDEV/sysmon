# Configuration file for the Sphinx documentation builder.
# Read the Docs: https://docs.readthedocs.io

import os
import sys

# -- Project information -----------------------------------------------------
project   = 'sysmon'
author    = 'sysmon contributors'
copyright = '2026, sysmon contributors'
release   = 'Unreleased'
version   = 'Unreleased'

# -- General configuration ---------------------------------------------------
extensions = [
    'sphinx.ext.autodoc',
    'sphinx.ext.viewcode',
    'sphinx.ext.githubpages',
    'myst_parser',           # Markdown support
]

source_suffix = {
    '.rst': 'restructuredtext',
    '.md':  'markdown',
}

templates_path = ['_templates']
exclude_patterns = ['_build', 'Thumbs.db', '.DS_Store']

# -- HTML output ------------------------------------------------------------
html_theme         = 'sphinx_rtd_theme'
html_static_path   = ['_static']
html_title         = 'sysmon Documentation'
html_short_title   = 'sysmon'
html_show_sourcelink = True
html_show_copyright  = True

# -- MyST options -----------------------------------------------------------
myst_enable_extensions = [
    'colon_fence',
    'deflist',
    'tasklist',
]
