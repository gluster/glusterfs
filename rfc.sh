#!/bin/sh -e
# Since we run with '#!/bin/sh -e'
#   '$? != 0' will force an exit,
# i.e. where we are interested in the result of a command,
# we have to run the command in an if-statement.


while getopts "v" opt; do
    case $opt in
        v)
            # Verbose mode
            git () { >&2 echo "git $@" && `which git` $@; }
            ;;
    esac
done
# Move the positional arguments to the beginning
shift $((OPTIND-1))


branch="master";

set_hooks_commit_msg()
{
    f=".git/hooks/commit-msg";
    u="http://review.gluster.org/tools/hooks/commit-msg";

    if [ -x "$f" ]; then
        return;
    fi

    curl -L -o $f $u || wget -O $f $u;

    chmod +x $f

    # Let the 'Change-Id: ' header get assigned on first run of rfc.sh
    GIT_EDITOR=true git commit --amend;
}


is_num()
{
    local num;

    num="$1";

    [ -z "$(echo $num | sed -e 's/[0-9]//g')" ]
}


rebase_changes()
{
    GIT_EDITOR=$0 git rebase -i origin/$branch;
}


editor_mode()
{
    if [ $(basename "$1") = "git-rebase-todo" ]; then
        sed 's/^pick /reword /g' "$1" > $1.new && mv $1.new $1;
        return;
    fi

    if [ $(basename "$1") = "COMMIT_EDITMSG" ]; then
        if grep -qi '^BUG: ' $1; then
            return;
        fi
        while true; do
            echo Commit: "\"$(head -n 1 $1)\""
            echo -n "Enter Bug ID: "
            read bug
            if [ -z "$bug" ]; then
                return;
            fi
            if ! is_num "$bug"; then
                echo "Invalid Bug ID ($bug)!!!";
                continue;
            fi

            sed "/^Change-Id:/{p; s/^.*$/BUG: $bug/;}" $1 > $1.new && \
                mv $1.new $1;
            return;
        done
    fi

    cat <<EOF
$0 - editor_mode called on unrecognized file $1 with content:
$(cat $1)
EOF
    return 1;
}


assert_diverge()
{
    git diff origin/$branch..HEAD | grep -q .;
}


check_patches_for_coding_style()
{
    git fetch origin;

    check_patch_script=./extras/checkpatch.pl
    if [ ! -e ./extras/checkpatch.pl ] ; then
        echo "checkpatch is not executable .. abort"
        exit 1
    fi

    # The URL of our Gerrit server
    export GERRIT_URL="review.gluster.org"

    echo "Running coding guidelines check ..."
    head=$(git rev-parse --abbrev-ref HEAD)
    # Kludge: "1>&2 && echo $? || echo $?" is to get around
    #         "-e" from script invocation
    RES=$(git format-patch --stdout origin/${branch}..${head} \
          | ${check_patch_script} --terse - 1>&2 && echo $? || echo $?)
    if [ "$RES" -eq 1 ] ; then
        echo "Errors caught, get details by:"
        echo "  git format-patch --stdout  origin/${branch}..${head} \\"
        echo "  | ./extras/checkpatch.pl --gerrit-url ${GERRIT_URL} -"
        echo "and correct errors"
        exit 1
    elif [ "$RES" -eq 2 ] ; then
        echo "Warnings caught, get details by:"
        echo "  git format-patch --stdout  origin/${branch}..${head} \\"
        echo "  | ./extras/checkpatch.pl --gerrit-url ${GERRIT_URL} -"
        echo -n "Do you want to continue anyway [no/yes]: "
        read yesno
        if [ "${yesno}" != "yes" ] ; then
            echo "Aborting..."
            exit 1
        fi
    fi
}


main()
{
    set_hooks_commit_msg;

    if [ -e "$1" ]; then
        editor_mode "$@";
        return;
    fi

    check_patches_for_coding_style;

    rebase_changes;

    assert_diverge;

    bug=$(git show --format='%b' | grep -i '^BUG: ' | awk '{print $2}');

    if [ "$DRY_RUN" = 1 ]; then
        drier='echo -e Please use the following command to send your commits to review:\n\n'
    else
        drier=
    fi

    if [ -z "$bug" ]; then
        $drier git push origin HEAD:refs/for/$branch/rfc;
    else
        $drier git push origin HEAD:refs/for/$branch/bug-$bug;
    fi
}

main "$@"
