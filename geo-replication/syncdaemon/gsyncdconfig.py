# -*- coding: utf-8 -*-
#
#  Copyright (c) 2016 Red Hat, Inc. <http://www.redhat.com>
#  This file is part of GlusterFS.
#
#  This file is licensed to you under your choice of the GNU Lesser
#  General Public License, version 3 or any later version (LGPLv3 or
#  later), or the GNU General Public License, version 2 (GPLv2), in all
#  cases as published by the Free Software Foundation.
#

try:
    from ConfigParser import RawConfigParser, NoSectionError
except ImportError:
    from configparser import RawConfigParser, NoSectionError
import os
import shutil
from string import Template
from datetime import datetime
from threading import Lock


# Global object which can be used in other modules
# once load_config is called
_gconf = {}


class GconfNotConfigurable(Exception):
    pass


class GconfInvalidValue(Exception):
    pass


class Gconf(object):
    def __init__(self, default_conf_file, custom_conf_file=None,
                 args={}, extra_tmpl_args={}, override_from_args=False):
        self.lock = Lock()
        self.default_conf_file = default_conf_file
        self.custom_conf_file = custom_conf_file
        self.tmp_conf_file = None
        self.gconf = {}
        self.gconfdata = {}
        self.gconf_typecast = {}
        self.template_conf = []
        self.non_configurable_configs = []
        self.prev_mtime = 0
        if custom_conf_file is not None:
            self.tmp_conf_file = custom_conf_file + ".tmp"

        self.session_conf_items = []
        self.args = args
        self.extra_tmpl_args = extra_tmpl_args
        self.override_from_args = override_from_args
        # Store default values only if overwritten, Only for JSON/CLI output
        self.default_values = {}
        self._load()

    def _tmpl_substitute(self):
        tmpl_values = {}
        for k, v in self.gconf.items():
            tmpl_values[k.replace("-", "_")] = v

        # override the config file values with the one user passed
        for k, v in self.args.items():
            # override the existing value only if set by user
            if v is not None:
                tmpl_values[k] = v

        for k, v in self.extra_tmpl_args.items():
            tmpl_values[k] = v

        for k, v in self.gconf.items():
            if k in self.template_conf and \
               (isinstance(v, str) or isinstance(v, unicode)):
                self.gconf[k] = Template(v).safe_substitute(tmpl_values)

    def _do_typecast(self):
        for k, v in self.gconf.items():
            cast_func = globals().get(
                "to_" + self.gconf_typecast.get(k, "string"), None)
            if cast_func is not None:
                self.gconf[k] = cast_func(v)
                if self.default_values.get(k, None) is not None:
                    self.default_values[k] = cast_func(v)

    def reset(self, name):
        # If custom conf file is not set then it is only read only configs
        if self.custom_conf_file is None:
            raise GconfNotConfigurable()

        # If a config can not be modified
        if name != "all" and not self._is_configurable(name):
            raise GconfNotConfigurable()

        cnf = RawConfigParser()
        with open(self.custom_conf_file) as f:
            cnf.readfp(f)

            # Nothing to Reset, Not configured
            if name != "all":
                if not cnf.has_option("vars", name):
                    return True

                # Remove option from custom conf file
                cnf.remove_option("vars", name)
            else:
                # Remove and add empty section, do not disturb if config file
                # already has any other section
                try:
                    cnf.remove_section("vars")
                except NoSectionError:
                    pass

                cnf.add_section("vars")

        with open(self.tmp_conf_file, "w") as fw:
            cnf.write(fw)

        os.rename(self.tmp_conf_file, self.custom_conf_file)

        self.reload()

        return True

    def set(self, name, value):
        if self.custom_conf_file is None:
            raise GconfNotConfigurable()

        if not self._is_configurable(name):
            raise GconfNotConfigurable()

        if not self._is_valid_value(name, value):
            raise GconfInvalidValue()

        curr_val = self.gconf.get(name, None)
        if curr_val == value:
            return True

        cnf = RawConfigParser()
        with open(self.custom_conf_file) as f:
            cnf.readfp(f)

        if not cnf.has_section("vars"):
            cnf.add_section("vars")

        cnf.set("vars", name, value)
        with open(self.tmp_conf_file, "w") as fw:
            cnf.write(fw)

        os.rename(self.tmp_conf_file, self.custom_conf_file)

        self.reload()

        return True

    def check(self, name, value=None, with_conffile=True):
        if with_conffile and self.custom_conf_file is None:
            raise GconfNotConfigurable()

        if not self._is_configurable(name):
            raise GconfNotConfigurable()

        if value is not None and not self._is_valid_value(name, value):
            raise GconfInvalidValue()


    def _load_with_lock(self):
        with self.lock:
            self._load()

    def _load(self):
        self.gconf = {}
        self.template_conf = []
        self.gconf_typecast = {}
        self.non_configurable_configs = []
        self.session_conf_items = []
        self.default_values = {}

        conf = RawConfigParser()
        # Default Template config file
        with open(self.default_conf_file) as f:
            conf.readfp(f)

        # Custom Config file
        if self.custom_conf_file is not None:
            with open(self.custom_conf_file) as f:
                conf.readfp(f)

        # Get version from default conf file
        self.version = conf.get("__meta__", "version")

        # Populate default values
        for sect in conf.sections():
            if sect in ["__meta__", "vars"]:
                continue

            # Collect list of available options with help details
            self.gconfdata[sect] = {}
            for k, v in conf.items(sect):
                self.gconfdata[sect][k] = v.strip()

            # Collect the Type cast information
            if conf.has_option(sect, "type"):
                self.gconf_typecast[sect] = conf.get(sect, "type")

            # Prepare list of configurable conf
            if conf.has_option(sect, "configurable"):
                if conf.get(sect, "configurable").lower() == "false":
                    self.non_configurable_configs.append(sect)

            # if it is a template conf value which needs to be substituted
            if conf.has_option(sect, "template"):
                if conf.get(sect, "template").lower().strip() == "true":
                    self.template_conf.append(sect)

            # Set default values
            if conf.has_option(sect, "value"):
                self.gconf[sect] = conf.get(sect, "value").strip()

        # Load the custom conf elements and overwrite
        if conf.has_section("vars"):
            for k, v in conf.items("vars"):
                self.session_conf_items.append(k)
                self.default_values[k] = self.gconf.get(k, "")
                self.gconf[k] = v.strip()

        # Overwrite the Slave configurations which are sent as
        # arguments to gsyncd slave
        if self.override_from_args:
            for k, v in self.args.items():
                k = k.replace("_", "-")
                if k.startswith("slave-") and k in self.gconf:
                    self.gconf[k] = v

        self._tmpl_substitute()
        self._do_typecast()

    def reload(self, with_lock=True):
        if self._is_config_changed():
            if with_lock:
                self._load_with_lock()
            else:
                self._load()

    def get(self, name, default_value=None, with_lock=True):
        if with_lock:
            with self.lock:
                return self.gconf.get(name, default_value)
        else:
            return self.gconf.get(name, default_value)

    def getall(self, show_defaults=False, show_non_configurable=False):
        cnf = {}
        if not show_defaults:
            for k in self.session_conf_items:
                if k not in self.non_configurable_configs:
                    dv = self.default_values.get(k, "")
                    cnf[k] = {
                        "value": self.get(k),
                        "default": dv,
                        "configurable": True,
                        "modified": False if dv == "" else True
                    }
            return cnf

        # Show all configs including defaults
        for k, v in self.gconf.items():
            configurable = False if k in self.non_configurable_configs \
                           else True
            dv = self.default_values.get(k, "")
            modified = False if dv == "" else True
            if show_non_configurable:
                cnf[k] = {
                    "value": v,
                    "default": dv,
                    "configurable": configurable,
                    "modified": modified
                }
            else:
                if k not in self.non_configurable_configs:
                    cnf[k] = {
                        "value": v,
                        "default": dv,
                        "configurable": configurable,
                        "modified": modified
                    }

        return cnf

    def getr(self, name, default_value=None):
        with self.lock:
            self.reload(with_lock=False)
            return self.get(name, default_value, with_lock=False)

    def get_help(self, name=None):
        pass

    def _is_configurable(self, name):
        item = self.gconfdata.get(name, None)
        if item is None:
            return False

        return item.get("configurable", True)

    def _is_valid_value(self, name, value):
        item = self.gconfdata.get(name, None)
        if item is None:
            return False

        # If validation func not defined
        if item.get("validation", None) is None:
            return True

        # minmax validation
        if item["validation"] == "minmax":
            return validate_minmax(value, item["min"], item["max"])

        if item["validation"] == "choice":
            return validate_choice(value, item["allowed_values"])

        if item["validation"] == "bool":
            return validate_bool(value)

        if item["validation"] == "execpath":
            return validate_execpath(value)

        if item["validation"] == "unixtime":
            return validate_unixtime(value)

        if item["validation"] == "int":
            return validate_int(value)

        return False

    def _is_config_changed(self):
        if self.custom_conf_file is not None and \
           os.path.exists(self.custom_conf_file):
                st = os.lstat(self.custom_conf_file)
                if st.st_mtime > self.prev_mtime:
                    self.prev_mtime = st.st_mtime
                    return True

        return False

def is_config_file_old(config_file, mastervol, slavevol):
    cnf = RawConfigParser()
    cnf.read(config_file)
    session_section = "peers %s %s" % (mastervol, slavevol)
    try:
        return dict(cnf.items(session_section))
    except NoSectionError:
        return None

def config_upgrade(config_file, ret):
    config_file_backup = os.path.join(os.path.dirname(config_file), "gsyncd.conf.bkp")

    #copy old config file in a backup file
    shutil.copyfile(config_file, config_file_backup)

    #write a new config file
    config = RawConfigParser()
    config.add_section('vars')

    for key, value in ret.items():
        #handle option name changes
        if key == "use_tarssh":
            new_key = "sync-method"
            if value == "true":
                new_value = "tarssh"
            else:
                new_value = "rsync"
            config.set('vars', new_key, new_value)
        elif key == "timeout":
            new_key = "slave-timeout"
            config.set('vars', new_key, value)
        #for changes like: ignore_deletes to ignore-deletes
        else:
            new_key = key.replace("_", "-")
            config.set('vars', new_key, value)

    with open(config_file, 'w') as configfile:
        config.write(configfile)


def validate_int(value):
    try:
        _ = int(value)
        return True
    except ValueError:
        return False


def validate_unixtime(value):
    try:
        y = datetime.fromtimestamp(int(value)).strftime("%Y")
        if y == "1970":
            return False

        return True
    except ValueError:
        return False


def validate_minmax(value, minval, maxval):
    try:
        value = int(value)
        minval = int(minval)
        maxval = int(maxval)
        return value >= minval and value <= maxval
    except ValueError:
        return False


def validate_choice(value, allowed_values):
    allowed_values = allowed_values.split(",")
    allowed_values = [v.strip() for v in allowed_values]

    return value in allowed_values


def validate_bool(value):
    return value in ["true", "false"]


def validate_execpath(value):
    return os.path.isfile(value) and os.access(value, os.X_OK)


def validate_filepath(value):
    return os.path.isfile(value)


def validate_path(value):
    return os.path.exists(value)


def to_int(value):
    return int(value)


def to_float(value):
    return float(value)


def to_bool(value):
    if isinstance(value, bool):
        return value
    return True if value in ["true", "True"] else False


def get(name, default_value=None):
    return _gconf.get(name, default_value)


def getall(show_defaults=False, show_non_configurable=False):
    return _gconf.getall(show_defaults=show_defaults,
                         show_non_configurable=show_non_configurable)


def getr(name, default_value=None):
    return _gconf.getr(name, default_value)


def load(default_conf, custom_conf=None, args={}, extra_tmpl_args={},
         override_from_args=False):
    global _gconf
    _gconf = Gconf(default_conf, custom_conf, args, extra_tmpl_args,
                   override_from_args)


def setconfig(name, value):
    global _gconf
    _gconf.set(name, value)


def resetconfig(name):
    global _gconf
    _gconf.reset(name)


def check(name, value=None, with_conffile=True):
    global _gconf
    _gconf.check(name, value=value, with_conffile=with_conffile)
