#!/bin/bash
# This script automates the steps required to "cut" a GlusterFS release
# Before running this script it assumes:
# - You've run all prove tests and they all pass, or have very good reasons for
#   ignoring any failures.
# - You have a working GlusterFS repo
# - You've installed any libraries which are required to build GlusterFS
# - You are _authorized_ by the storage to to cut a release, e-mail storage@fb.com
#   if you are unsure.
#

PUBLISH_REPO=false
TAG_RELEASE=true
BUILD_FROM_TAG=false
GFS_VER="3.8.15_fb"
function usage {
cat << EOF
Usage $0 [-p|-t|-n] <release tag>,

Where,
    -p     publish the RPMs to the site-packages repo.
    -n     suppress making git tag for this release (cannot be used with -p).
    -t     checkout tag and publish RPMs from this tag

e.g. "$0 1" creates a release called v3.6.3_fb-1

HINT: If you just want to create "test" RPMs, this isn't the script you are
      looking for; use buildrpm.sh for this.  This script is intended for
      releasing _production_ quality RPMs.

EOF
exit 1
}

(( $# == 0 )) && usage
while getopts ":apnt" options; do
  case $options in
    a )  echo "Hey Mr. Old-skool, '-a' is deprecated; use '-p' to publish your RPMs."
         echo "NB: unlike '-a', '-p' publishes RPMs *immediately*. Be sure that's what you want!"
         exit 1
         ;;
    p ) PUBLISH_REPO=true;;
    n ) TAG_RELEASE=false;;
    t ) BUILD_FROM_TAG=true;; 
   \? ) usage;;
    * )  usage;;
  esac
done

if [ $PUBLISH_REPO -a ! $TAG_RELEASE ]; then
   echo "Cannot publish release without tagging."
   exit 1
fi

if $BUILD_FROM_TAG; then 
    TAG_RELEASE=false
fi

RELEASE_TAG=${@:$OPTIND:1}

echo -n "Checking if user is root..."
if [ $USER == "root" ]; then
  echo yes
  echo "This script is not intended to be run by root, aborting!" && exit 1
else
  echo DONE
fi

echo -n "Checking if $USER is in storage group..."
if ! getent group storage | grep -q $USER; then
  echo "$USER not in storage group, aborting!" && exit 1
else
  echo DONE
fi

echo -n "Checking OS version..."
REDHAT_MAJOR=$(/usr/lib/rpm/redhat/dist.sh --distnum)

if [ "$REDHAT_MAJOR" = "6" -o "$REDHAT_MAJOR" = "7" ]; then
   echo DONE
else
   echo "You are treading unknown ground with Centos $REDHAT_MAJOR! You are likely to be eaten by a grue!"
   read -p "Press forward (y/n)? " yesno
   if [ "$yesno" != "y" ]; then
     exit 1
   fi
fi

echo -n "Checking for uncommitted changes..."
UNCOMMITTED_CHANGES=$(git status -s | grep -v "??")
if [ ! -z "$UNCOMMITTED_CHANGES" ]; then
    echo "FAILED"
    echo "You have changes in your repo. Commit them or stash them before building."
    exit 1;
fi

#echo "Updating repo..."
#if ! ( git fetch && git rebase ); then
#  echo "Unable to update GIT repo, aborting!" && exit 1
#fi
#if ! BUILD_VERSION=$(grep AC_INIT.*glusterfs configure.ac | cut -d, -f2 | grep -Eo "[0-9A-Za-z._]+"); then
#  echo "Unable to find build version, aborting!" && exit 1
#fi
BUILD_VERSION=3.8.15_fb
echo "Build version is $BUILD_VERSION..."
echo "Release tag is $RELEASE_TAG..."
GIT_TAG="v$BUILD_VERSION-$RELEASE_TAG"
if [ $TAG_RELEASE -o $BUILD_FROM_TAG ]; then
    echo -n "Checking for conflicts for tag $GIT_TAG..."
    if git tag | grep -qw $GIT_TAG; then
      if ! $BUILD_FROM_TAG; then
        echo "FAILED"
        echo "Gluster release $GIT_TAG already exists, please try again, or pass -t to build from this tag!" && exit 1
      else
        if ! git checkout $GIT_TAG; then
            echo "FAILED"
            echo "Failed to checkout $GIT_TAG."
            exit 1
        fi
      fi
    fi
    echo DONE
fi
echo "Building RPMs..."
if ! ./buildrpm38.sh $RELEASE_TAG; then
  echo "Failed to build RPMs, aborting!" && exit 1
fi

if $TAG_RELEASE; then
    echo "Creating GIT tag for this build..."
    if ! git tag -a $GIT_TAG -m "GlusterFS build $GIT_TAG"; then
      echo "Unable to tag build, aborting!" && exit 1
    fi
fi

if $PUBLISH_REPO; then
  echo "Publishing RPMs..."
  if ! svnyum -y publish site-packages/${REDHAT_MAJOR}/x86_64 ~/local/rpmbuild/RPMS/x86_64/glusterfs*${GFS_VER}_fb-${RELEASE_TAG}.el${REDHAT_MAJOR}.x86_64.rpm; then
    echo "ERROR: Unable to publish RPMs!"
    echo "Removing GIT tag ($GIT_TAG) from GFS (local) git repo..."
    git tag -d $GIT_TAG && echo "Removing tag $GIT_TAG, and aborting."
    exit 1
  fi
fi

if $TAG_RELEASE; then
    echo "Pushing tag to remote repo..."
    if ! git push origin $GIT_TAG; then
      echo "Unable to push tag to repo, aborting!" && exit 1
    fi
fi

echo "Successfully released GlusterFS $GIT_TAG!"
