# Configuration file for the Sphinx documentation builder.
# Read the Docs: https://docs.readthedocs.io

import os
import sys

# -- Project information -----------------------------------------------------
project   = 'sysmon'
author    = 'sysmon contributors'
copyright = '2025, sysmon contributors'
release   = '0.2.0'
version   = '0.2'

# -- General configuration ---------------------------------------------------
extensions = [
    'sphinx.ext.autodoc',
    'sphinx.ext.viewcode',
    'sphinx.ext.githubpages',
    'myst_parser',           # Markdown support
    'breathe',               # Doxygen XML → Sphinx (optional)
]

source_suffix = {
    '.rst': 'restructuredtext',
    '.md':  'markdown',
}

templates_path = ['_templates']
exclude_patterns = ['_build', 'Thumbs.db', '.DS_Store']

# -- Breathe (Doxygen integration) -------------------------------------------
breathe_projects = {
    'sysmon': '../doxygen/xml',
}
breathe_default_project = 'sysmon'

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
