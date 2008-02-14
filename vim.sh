#!/bin/sh
ctags -R .
gvim SConstruct src/*
