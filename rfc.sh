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

backport_id_message()
{
    echo ""
    echo "This commit is to a non-master branch, and hence is treated as a backport."
    echo ""
    echo "For backports we would like to retain the same gerrit Change-Id across"
    echo "branches. On auto inspection it is found that a gerrit Change-Id is"
    echo "missing, or the Change-Id is not found on your local master"
    echo ""
    echo "This could mean a few things:"
    echo "    1. This is not a backport, hence choose Y on the prompt to proceed"
    echo "    2. Your origin master is not up to date, hence the script is unable"
    echo "       to find the corresponding Change-Id on master. Either choose N,"
    echo "       'git fetch', and try again, OR if you are sure you used the"
    echo "       same Change-Id, choose Y at the prompt to proceed"
    echo "    3. You commented or removed the Change-Id in your commit message after"
    echo "       cherry picking the commit. Choose N, fix the commit message to"
    echo "       use the same Change-Id as master (git commit --amend), resubmit"
    echo ""
}

check_backport()
{
    moveon='N'

    # Backports are never made to master
    if [ $branch = "master" ]; then
        return;
    fi

    # Extract the change ID from the commit message
    changeid=$(git show --format='%b' | grep -i '^Change-Id: ' | awk '{print $2}')

    # If there is no change ID ask if we should continue
    if [ -z "$changeid" ]; then
        backport_id_message;
        echo -n "Did not find a Change-Id for a possible backport. Continue (y/N): "
        read moveon
    else
        # Search master for the same change ID (rebase_changes has run, so we
        # should never not find a Change-Id)
        mchangeid=$(git log origin/master --format='%b' --grep="^Change-Id: ${changeid}" | grep ${changeid} | awk '{print $2}')

        # Check if we found the change ID on master, else throw a message to
        # decide if we should continue.
        # NOTE: If master was not rebased, we will not find the Change-ID and
        # could hit a false positive case here (or if someone checks out some
        # other branch as master).
        if [ "${mchangeid}" = "${changeid}" ]; then
            moveon="Y"
        else
            backport_id_message;
            echo "Change-Id of commit: $changeid"
            echo "Change-Id on master: $mchangeid"
            echo -n "Did not find mentioned Change-Id on master for a possible backport. Continue (y/N): "
            read moveon
        fi
    fi

    if [ "${moveon}" = 'Y' ] || [ "${moveon}" = 'y' ]; then
        return;
    else
        exit 1
    fi
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

    check_patch_script=./build-aux/checkpatch.pl
    if [ ! -e ${check_patch_script} ] ; then
        echo "${check_patch_script} is not executable .. abort"
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
        echo "  | ${check_patch_script} --gerrit-url ${GERRIT_URL} -"
        echo "and correct errors"
        exit 1
    elif [ "$RES" -eq 2 ] ; then
        echo "Warnings caught, get details by:"
        echo "  git format-patch --stdout  origin/${branch}..${head} \\"
        echo "  | ${check_patch_script} --gerrit-url ${GERRIT_URL} -"
        echo -n "Do you want to continue anyway [no/yes]: "
        read yesno
        if [ "${yesno}" != "yes" ] ; then
            echo "Aborting..."
            exit 1
        fi
    fi
}

github_issue_message()
{
    echo ""
    echo "=== Missing a github issue reference in a potential enhancement! ==="
    echo ""
    echo "Gluster code submissions that are enhancements (IOW, not functional"
    echo "bug fixes, but improvements of any nature to the code) are tracked"
    echo "using github issues. A check on the commit message, reveals that"
    echo "there is no bug associated with this change, hence it could be a"
    echo "potential code improvement or feature enhancement"
    echo ""
    echo "If this is an enhancement, request a github issue be filed at [1]"
    echo "and referenced in the commit message as,"
    echo "\"Fixes gluster/glusterfs#n\" OR \"Updates gluster/glusterfs#n\","
    echo "where n is the issue number"
    echo ""
    echo "You can reference multiple issues that this commit addresses as,"
    echo "\"fixes gluster/glusterfs#n, updates gluster/glusterfs#m\", and so on"
    echo ""
    echo "[1] https://github.com/gluster/glusterfs/issues/new"
    echo ""
    echo "You may abort the submission choosing 'N' below and use"
    echo "'git commit --amend' to add the issue reference before posting"
    echo "to gerrit. If this is a bug fix, choose 'Y' to continue."
    echo ""
}

check_for_github_issue()
{
    # NOTE: Since we run '#!/bin/sh -e', the check is in an if,
    # as grep count maybe 0
    #
    # Regex elaborated:
    #   grep -w -> --word-regexp (from the man page)
    #      Select only those lines containing matches that form whole words.
    #      The test is that the matching substring must either be at the
    #      beginning of the line, or preceded by a  non-word  constituent
    #      character.  Similarly, it must be either at the end of the line or
    #      followed by a non-word constituent character.  Word-constituent
    #      characters are letters, digits, and the underscore.
    #   IOW, the above helps us find the pattern with leading or training spaces
    #   or non word consituents like , or ;
    #
    #   grep -c -> gives us a count of matches, which is all we need here
    #
    #   [fF][iI][xX][eE][sS]|[uU][pP][dD][aA][tT][eE][sS])
    #      Finds 'fixes' OR 'updates' in any case combination
    #
    #   (:)?
    #      Followed by an optional : (colon)
    #
    #   [[:space:]]+
    #      followed by 1 or more spaces
    #
    #   (gluster\/glusterfs)?
    #      Followed by 0 or more gluster/glusterfs
    #
    #   #
    #      Followed by #
    #
    #   [[:digit:]]+
    #      Followed by 1 or more digits
    if [ 0 = "$(git log --format=%B -n 1 | grep -cow -E "([fF][iI][xX][eE][sS]|[uU][pP][dD][aA][tT][eE][sS])(:)?[[:space:]]+(gluster\/glusterfs)?#[[:digit:]]+")" ]; then
        moveon='N'
        github_issue_message;
        echo -n "Missing github issue reference in a potential RFE. Continue (y/N): "
        read moveon
        if [ "${moveon}" = 'Y' ] || [ "${moveon}" = 'y' ]; then
            return;
        else
            exit 1
        fi
    fi
}

main()
{
    set_hooks_commit_msg;

    # rfc.sh calls itself from rebase_changes, which uses rfc.sh as the EDITOR
    # thus, getting the commit message to work with in the editor_mode.
    if [ -e "$1" ]; then
        editor_mode "$@";
        return;
    fi

    check_patches_for_coding_style;

    rebase_changes;

    check_backport;

    assert_diverge;

    bug=$(git show --format='%b' | grep -i '^BUG: ' | awk '{print $2}');

    # If this is a commit against master and does not have a bug ID
    # it could be a feature or an RFE, check if there is a github
    # issue reference, and if not suggest commit message amendment
    if [ -z "$bug" ] && [ $branch = "master" ]; then
        check_for_github_issue;
    fi


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
