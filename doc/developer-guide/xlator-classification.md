# xlator categories and expectations

The purpose of the document is to define a category for various xlators
and expectations around what each category means from a perspective of
health and maintenance of a xlator.

The need to do this is to ensure certain categories are kept in good
health, and helps the community and contributors focus their efforts around the
same.

This document also provides implementation details for xlator developers to
declare a category for any xlator.

## Table of contents
1. Audience
2. Categories (and expectations of each category)
3. Implementation and usage details

## Audience

This document is intended for the following community participants,
- New xlator contributors
- Existing xlator maintainers
- Packaging and gluster management stack maintainers

For a more user facing understanding it is recommended to read section (TBD)
in the gluster documentation.

## Categories
1. Experimental (E)
2. TechPreview  (TP)
3. Maintained (M)
4. Deprecated (D)
5. Obsolete (O)

### Experimental (E)

Developed in the experimental branch, for exploring new features. These xlators
are NEVER packaged as a part of releases, interested users and contributors can
build and work with these from sources. In the future, these maybe available as
an package based on a weekly build of the same.

#### Quality expectations
- Compiles or passes smoke tests
- Does not break nightly experimental regressions
  - NOTE: If a nightly is broken, then all patches that were merged are reverted
  till the errant patch is found and subsequently fixed

### TechPreview (TP)

Xlators in master or release branches that are not deemed fit to be in
production deployments, but are feature complete to invite feedback and host
user data.

These xlators will be worked upon with priority by maintainers/authors who are
involved in making them more stable than xlators in the Experimental/Deprecated/
Obsolete categories.

There is no guarantee that these xlators will move to the Maintained state, and
may just get Obsoleted based on feedback, or other project goals or technical
alternatives.

#### Quality expectations
- Same as Maintained, minus
  - Performance, Scale, other(?)
  - *TBD* *NOTE* Need inputs, Intention is all quality goals as in Maintained,
  other than the list above (which for now has scale and performance)

### Maintained (M)

These xltors are part of the core Gluster functionality and are maintained
actively. These are part of master and release branches and are higher in
priority of maintainers and other interested contributors.

#### Quality expectations

NOTE: A short note on what each of these mean are added here, details to follow.

NOTE: Out of the gate all of the following are not mandated, consider the
following a desirable state to reach as we progress on each

- Bug backlog: Actively address bug backlog
- Enhancement backlog: Actively maintain outstanding enhancement backlog (need
        not be acted on, but should be visible to all)
- Review backlog: Actively keep this below desired counts and states
- Static code health:  Actively meet near-zero issues in this regard
  - Coverity, spellcheck and other checks
- Runtime code health: Actively meet defined coverage levels in this regard
  - Coverage, others?
  - Per-patch regressions
  - Glusto runs
  - Performance
  - Scalability
- Technical specifications: Implementation details should be documented and
        updated at regular cadence (even per patch that change assumptions in
        here)
- User documentation: User facing details should be maintained to current
        status in the documentation
- Debuggability: Steps, tools, procedures should be documented and maintained
        each release/patch as applicable
- Troubleshooting: Steps, tools, procedures should be documented and maintained
        each release/patch as applicable
  - Steps/guides for self service
  - Knowledge base for problems
- Other common criteria that will apply: Required metrics/desired states to be
        defined per criteria
  - Monitoring, usability, statedump, and other such xlator expectations

### Deprecated (D)

Xlators on master or release branches that would be obsoleted and/or replaced
with similar or other functionality in the next major release.

#### Quality expectations
- Retain status-quo when moved to this state, till it is moved to obsoleted
- Provide migration steps if feature provided by the xlator is replaced with
other xlators

### Obsolete (O)

Xlator/code still in tree, but not packaged or shipped or maintained in any
form. This is noted as a category till the code is removed from the tree.

These xlators and their corresponding code and test health will not be executed.

#### Quality expectations
- None

## Implementation and usage details

### How to specify an xlators category

While defining 'xlator_api_t' structure for the corresponding xlator, add a
flag like below:

```
diff --git a/xlators/performance/nl-cache/src/nl-cache.c b/xlators/performance/nl-cache/src/nl-cache.c
index 0f0e53bac2..8267d6897c 100644
--- a/xlators/performance/nl-cache/src/nl-cache.c
+++ b/xlators/performance/nl-cache/src/nl-cache.c
@@ -869,4 +869,5 @@ xlator_api_t xlator_api = {
         .cbks          = &nlc_cbks,
         .options       = nlc_options,
         .identifier    = "nl-cache",
+        .category      = GF_TECH_PREVIEW,
 };
diff --git a/xlators/performance/quick-read/src/quick-read.c b/xlators/performance/quick-read/src/quick-read.c
index 8d39720e7f..235de27c19 100644
--- a/xlators/performance/quick-read/src/quick-read.c
+++ b/xlators/performance/quick-read/src/quick-read.c
@@ -1702,4 +1702,5 @@ xlator_api_t xlator_api = {
         .cbks          = &qr_cbks,
         .options       = qr_options,
         .identifier    = "quick-read",
+        .category      = GF_MAINTAINED,
 };
```

Similarly, if a particular option is in different state other than
the xlator state, one can add the same flag in options structure too.

```
diff --git a/xlators/cluster/afr/src/afr.c b/xlators/cluster/afr/src/afr.c
index 0e86e33d03..81996743d1 100644
--- a/xlators/cluster/afr/src/afr.c
+++ b/xlators/cluster/afr/src/afr.c
@@ -772,6 +772,7 @@ struct volume_options options[] = {
           .description = "Maximum latency for shd halo replication in msec."
         },
         { .key   = {"halo-enabled"},
+          .category = GF_TECH_PREVIEW,
           .type  = GF_OPTION_TYPE_BOOL,
           .default_value = "False",

```


### User experience using the categories

#### Ability to use a category

This section details which category of xlators can be used when and specifics
around when each category is enabled.

1. Maintained category xlators can be used by default, this implies, volumes
created with these xlators enabled will throw no warnings, or need no user
intervention to use the xlator.

2. Tech Preview category xlators needs cluster configuration changes to allow
these xlatorss to be used in volumes, further, logs will contain a message
stating TP xlators are in use. Without the cluster configured to allow TP
xlators, volumes created or edited to use such xlators would result in errors.
  - (TBD) Cluster configuration option
  - (TBD) Warning message
  - (TBD) Code mechanics on how this is achieved

3. Deprecated category xlators can be used by default, but will throw a warning
in the logs that such are in use and will be deprecated in the future.
  - (TBD) Warning message

4. Obsolete category xlators will not be packaged and hence cannot be used from
release builds.

5. Experimental category xlators will not be packaged and hence cannot be used
from release builds, if running experimental (weekly or other such) builds,
these will throw a warning in the logs stating experimental xlators are in use.
  - (TBD) Warning message

#### Ability to query xlator category

(TBD) Need to provide the ability to query xlator categories, or list xlators
and their respective categories.

#### User facing changes

User facing changes that are expected due to this change include the following,
- Cluster wide option to enable TP xlators, or more generically a category
level of xlators
- Errors in commands that fail due to invalid categories
- Warning messages in logs to denote certain categories of xlators are in use
- (TBD) Ability to query xlators and their respective categories
