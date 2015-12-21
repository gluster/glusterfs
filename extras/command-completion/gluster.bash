#!/bin/bash

if pidof glusterd > /dev/null 2>&1; then
        GLUSTER_SET_OPTIONS="
        $(for token in `gluster volume set help 2>/dev/null | grep "^Option:" | cut -d ' ' -f 2`
        do
                echo "{$token},"
        done)
        "
        GLUSTER_RESET_OPTIONS="$GLUSTER_SET_OPTIONS"
fi

GLUSTER_TOP_SUBOPTIONS1="
        {nfs},
        {brick},
        {list-cnt}
"
GLUSTER_TOP_SUBOPTIONS2="
        {bs
                {__SIZE
                        {count}
                }
        },
        {brick},
        {list-cnt}
"
GLUSTER_TOP_OPTIONS="
        {open
                [ $TOP_SUBOPTIONS1 ]
        },
        {read
                [ $TOP_SUBOPTIONS1 ]
        },
        {write
                [ $TOP_SUBOPTIONS1 ]
        },
        {opendir
                [ $TOP_SUBOPTIONS1 ]
        },
        {readdir
                [ $TOP_SUBOPTIONS1 ]
        },
        {clear
                [ $TOP_SUBOPTIONS1 ]
        },
        {read-perf
                [ $TOP_SUBOPTIONS2 ]
        },
        {write-perf
                [ $TOP_SUBOPTIONS2 ]
        }
"

GLUSTER_QUOTA_OPTIONS="
        {enable},
        {disable},
        {list},
        {remove},
        {default-soft-limit},
        {limit-usage},
        {alert-time},
        {soft-timeout},
        {hard-timeout}
"

GLUSTER_PROFILE_OPTIONS="
        {start},
        {info [
                {peek},
                {incremental
                        {peek}
                },
                {cumulative},
                {clear},
              ]
        },
        {stop}
"

GLUSTER_BARRIER_OPTIONS="
        {enable},
        {disable}
"

GLUSTER_GEO_REPLICATION_SUBOPTIONS="
"
GLUSTER_GEO_REPLICATION_OPTIONS="
        {__VOLNAME [
                {__SLAVEURL [
                        {create [
                                {push-pem
                                        {force}
                                },
                                {force}
                                ]
                        },
                        {start {force} },
                        {status {detail} },
                        {config},
                        {pause {force} },
                        {resume {force} },
                        {stop {force} },
                        {delete {force} }
                            ]
                },
                {status}
                   ]
        },
        {status}
"

GLUSTER_VOLUME_OPTIONS="
        {volume [
                {add-brick
                        {__VOLNAME}
                },
                {barrier
                        {__VOLNAME
                                [ $GLUSTER_BARRIER_OPTIONS ]
                        }
                },
                {clear-locks
                        {__VOLNAME}
                },
                {create},
                {delete
                        {__VOLNAME}
                },
                {geo-replication
                        [ $GLUSTER_GEO_REPLICATION_OPTIONS ]
                },
                {heal
                        {__VOLNAME}
                },
                {help},
                {info
                        {__VOLNAME}
                },
                {list},
                {log
                        {__VOLNAME}
                },
                {profile
                        {__VOLNAME
                                [ $GLUSTER_PROFILE_OPTIONS ]
                        }
                },
                {quota
                        {__VOLNAME
                                [ $GLUSTER_QUOTA_OPTIONS ]
                        }
                },
                {rebalance
                        {__VOLNAME}
                },
                {remove-brick
                        {__VOLNAME}
                },
                {replace-brick
                        {__VOLNAME}
                },
                {reset
                        {__VOLNAME
                                [ $GLUSTER_RESET_OPTIONS ]
                        }
                },
                {set
                        {__VOLNAME
                                [ $GLUSTER_SET_OPTIONS ]
                        }
                },
                {start
                        {__VOLNAME
                                {force}
                        }
                },
                {statedump
                        {__VOLNAME}
                },
                {status
                        {__VOLNAME}
                },
                {stop
                        {__VOLNAME
                                {force}
                        }
                },
                {sync
                        {__HOSTNAME}
                },
                {top
                        {__VOLNAME
                                [ $GLUSTER_TOP_OPTIONS ]
                        }
                }
                ]
        }
"

GLUSTER_COMMAND_TREE="
{gluster [
        $GLUSTER_VOLUME_OPTIONS ,
        {peer [
              {probe
                      {__HOSTNAME}
              },
              {detach
                      {__HOSTNAME
                                {force}
                      }
              },
              {status}
              ]
        },
        {pool
                {list}
        },
        {help}
        ]
}"

__SIZE ()
{
        return 0
}

__SLAVEURL ()
{
        return 0
}

__HOSTNAME ()
{
        local zero=0
        local ret=0
        local cur_word="$2"

        if [ "$1" == "X" ]; then
                return

        elif [ "$1" == "match" ]; then
                return 0

        elif [ "$1" == "complete" ]; then
                COMPREPLY=($(compgen -A hostname -- $cur_word))
        fi
        return 0
}

__VOLNAME ()
{
        local zero=0
        local ret=0
        local cur_word="$2"
        local list=""

        if [ "X$1" == "X" ]; then
                return

        elif [ "$1" == "match" ]; then
                return 0

        elif [ "$1" == "complete" ]; then
                if ! pidof glusterd > /dev/null 2>&1; then
                        list='';

                else
                        list=`gluster volume list 2> /dev/null`
                fi

        else
                return 0
        fi

        COMPREPLY=($(compgen -W "$list" -- $cur_word))
        return 0
}

_gluster_throw () {
#echo $1 >&2
        COMPREPLY=''
        exit
}

declare FINAL_LIST=''
declare LIST=''
declare -i TOP=0
_gluster_push () {
        TOP=$((TOP + 1))
        return $TOP
}
_gluster_pop () {
        TOP=$((TOP - 1))
        return $TOP
}

_gluster_goto_end ()
{
        local prev_top=$1
        local top=$1
        local token=''

        while [ $top -ge $prev_top ]; do
                read -r token
                case $token in
                '{' | '[')
                        _gluster_push
                        top=$?
                        ;;
                '}' | ']')
                        _gluster_pop
                        top=$?
                        ;;
                esac
        done

        return
}

_gluster_form_list ()
{
        local token=''
        local top=0
        local comma=''
        local cur_word="$1"

        read -r token
        case $token in
        ']')
                ;;
        '{')
                _gluster_push
                top=$?
                read -r key
                if [ "X$cur_word" == "X" -o "${cur_word:0:1}" == "${key:0:1}" -o "${key:0:1}" == "_" ]; then
                        LIST="$LIST $key"
                fi

                _gluster_goto_end $top
                read -r comma
                if [ "$comma" == "," ]; then
                        _gluster_form_list $cur_word
                fi
                ;;
        *)
                _gluster_throw "Expected '{' but received $token"
                ;;
        esac

        return
}

_gluster_goto_child ()
{
        local match_string="$1"
        local token=''
        local top=0
        local comma=''

        read -r token
        case $token in
        '{')
                _gluster_push
                top=$?
                ;;
        *)
                _gluster_throw "Expected '{' but received $token"
                ;;
        esac

        read -r token
        case `echo $token` in
        '[' | ']' | '{' | '}')
                _gluster_throw "Expected string but received $token"
                ;;
        _*)
                $token "match" $match_string
                ret=$?
                if [ $ret -eq 0 ]; then
                        return
                else
                        _gluster_goto_end $top

                        read -r comma
                        if [ "$comma" == "," ]; then
                                _gluster_goto_child $match_string
                        fi
                fi
                ;;

        "$match_string")
                return
                ;;
        *)
                _gluster_goto_end $top

                read -r comma
                if [ "$comma" == "," ]; then
                        _gluster_goto_child $match_string
                fi
                ;;
        esac

        return
}

_gluster_does_match ()
{
        local token="$1"
        local key="$2"

        if [ "${token:0:1}" == "_" ]; then
                $token $2
                return $?
        fi

        [ "$token" == "$key" ] && return 0

        return 1
}

_gluster_parse ()
{
        local i=0
        local token=''
        local tmp_token=''
        local word=''

        while [ $i -lt $COMP_CWORD ]; do
                read -r token
                case $token in
                '[')
                        _gluster_push
                        _gluster_goto_child ${COMP_WORDS[$i]}
                        ;;
                '{')
                        _gluster_push
                        read -r tmp_token
                        _gluster_does_match $tmp_token ${COMP_WORDS[$i]}
                        if [ $? -ne 0 ]; then
                                _gluster_throw "No match"
                        fi
                        ;;
                esac
                i=$((i+1))
        done

        read -r token
        if [ "$token" == '[' ]; then
                _gluster_push
                _gluster_form_list ${COMP_WORDS[COMP_CWORD]}

        elif [ "$token" == '{' ]; then
                read -r tmp_token
                LIST="$tmp_token"
        fi

        echo $LIST
}

_gluster_handle_list ()
{
        local list="${!1}"
        local cur_word=$2
        local count=0
        local i=0

        for i in `echo $list`; do
                count=$((count + 1))
        done

        if [ $count -eq 1 ] && [ "${i:0:1}" == "_" ]; then
                $i "complete" $cur_word
        else
                COMPREPLY=($(compgen -W "$list" -- $cur_word))
        fi
        return
}

_gluster_completion ()
{
        FINAL_LIST=`echo $GLUSTER_COMMAND_TREE |                              \
                egrep -ao --color=never "([A-Za-z0-9_.-]+)|[[:space:]]+|." |  \
                        egrep -v --color=never "^[[:space:]]*$" |             \
                                _gluster_parse`

        ARG="FINAL_LIST"
        _gluster_handle_list $ARG ${COMP_WORDS[COMP_CWORD]}
        return
}

complete -F _gluster_completion gluster
