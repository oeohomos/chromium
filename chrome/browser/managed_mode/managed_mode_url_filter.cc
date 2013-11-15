// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/managed_mode/managed_mode_url_filter.h"

#include "base/containers/hash_tables.h"
#include "base/files/file_path.h"
#include "base/json/json_file_value_serializer.h"
#include "base/metrics/histogram.h"
#include "base/sha1.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/task_runner_util.h"
#include "base/threading/sequenced_worker_pool.h"
#include "chrome/browser/policy/url_blacklist_manager.h"
#include "content/public/browser/browser_thread.h"
#include "extensions/common/matcher/url_matcher.h"
#include "net/base/registry_controlled_domains/registry_controlled_domain.h"
#include "url/gurl.h"

using content::BrowserThread;
using extensions::URLMatcher;
using extensions::URLMatcherConditionSet;
using net::registry_controlled_domains::EXCLUDE_UNKNOWN_REGISTRIES;
using net::registry_controlled_domains::EXCLUDE_PRIVATE_REGISTRIES;
using net::registry_controlled_domains::GetRegistryLength;

struct ManagedModeURLFilter::Contents {
  URLMatcher url_matcher;
  std::map<URLMatcherConditionSet::ID, int> matcher_site_map;
  base::hash_multimap<std::string, int> hash_site_map;
  std::vector<ManagedModeSiteList::Site> sites;
};

namespace {

const char* kStandardSchemes[] = {
  "http",
  "https",
  "file",
  "ftp",
  "gopher",
  "ws",
  "wss"
};


// This class encapsulates all the state that is required during construction of
// a new ManagedModeURLFilter::Contents.
class FilterBuilder {
 public:
  FilterBuilder();
  ~FilterBuilder();

  // Adds a single URL pattern for the site identified by |site_id|.
  bool AddPattern(const std::string& pattern, int site_id);

  // Adds a single hostname SHA1 hash for the site identified by |site_id|.
  void AddHostnameHash(const std::string& hash, int site_id);

  // Adds all the sites in |site_list|, with URL patterns and hostname hashes.
  void AddSiteList(ManagedModeSiteList* site_list);

  // Finalizes construction of the ManagedModeURLFilter::Contents and returns
  // them. This method should be called before this object is destroyed.
  scoped_ptr<ManagedModeURLFilter::Contents> Build();

 private:
  scoped_ptr<ManagedModeURLFilter::Contents> contents_;
  URLMatcherConditionSet::Vector all_conditions_;
  URLMatcherConditionSet::ID matcher_id_;
};

FilterBuilder::FilterBuilder()
    : contents_(new ManagedModeURLFilter::Contents()),
      matcher_id_(0) {}

FilterBuilder::~FilterBuilder() {
  DCHECK(!contents_.get());
}

bool FilterBuilder::AddPattern(const std::string& pattern, int site_id) {
  DCHECK(BrowserThread::GetBlockingPool()->RunsTasksOnCurrentThread());
  std::string scheme;
  std::string host;
  uint16 port;
  std::string path;
  bool match_subdomains = true;
  if (!policy::URLBlacklist::FilterToComponents(
          pattern, &scheme, &host, &match_subdomains, &port, &path)) {
    LOG(ERROR) << "Invalid pattern " << pattern;
    return false;
  }

  scoped_refptr<extensions::URLMatcherConditionSet> condition_set =
      policy::URLBlacklist::CreateConditionSet(
          &contents_->url_matcher, ++matcher_id_,
          scheme, host, match_subdomains, port, path);
  all_conditions_.push_back(condition_set);
  contents_->matcher_site_map[matcher_id_] = site_id;
  return true;
}

void FilterBuilder::AddHostnameHash(const std::string& hash, int site_id) {
  contents_->hash_site_map.insert(std::make_pair(StringToUpperASCII(hash),
                                                 site_id));
}

void FilterBuilder::AddSiteList(ManagedModeSiteList* site_list) {
  std::vector<ManagedModeSiteList::Site> sites;
  site_list->GetSites(&sites);
  int site_id = contents_->sites.size();
  for (std::vector<ManagedModeSiteList::Site>::const_iterator it =
           sites.begin(); it != sites.end(); ++it) {
    const ManagedModeSiteList::Site& site = *it;
    contents_->sites.push_back(site);

    for (std::vector<std::string>::const_iterator pattern_it =
             site.patterns.begin();
         pattern_it != site.patterns.end(); ++pattern_it) {
      AddPattern(*pattern_it, site_id);
    }

    for (std::vector<std::string>::const_iterator hash_it =
             site.hostname_hashes.begin();
         hash_it != site.hostname_hashes.end(); ++hash_it) {
      AddHostnameHash(*hash_it, site_id);
    }

    site_id++;
  }
}

scoped_ptr<ManagedModeURLFilter::Contents> FilterBuilder::Build() {
  DCHECK(BrowserThread::GetBlockingPool()->RunsTasksOnCurrentThread());
  contents_->url_matcher.AddConditionSets(all_conditions_);
  return contents_.Pass();
}

scoped_ptr<ManagedModeURLFilter::Contents> CreateWhitelistFromPatterns(
    const std::vector<std::string>& patterns) {
  DCHECK(BrowserThread::GetBlockingPool()->RunsTasksOnCurrentThread());

  FilterBuilder builder;
  for (std::vector<std::string>::const_iterator it = patterns.begin();
       it != patterns.end(); ++it) {
    // TODO(bauerb): We should create a fake site for the whitelist.
    builder.AddPattern(*it, -1);
  }

  return builder.Build();
}

scoped_ptr<ManagedModeURLFilter::Contents> LoadWhitelistsOnBlockingPoolThread(
    ScopedVector<ManagedModeSiteList> site_lists) {
  DCHECK(BrowserThread::GetBlockingPool()->RunsTasksOnCurrentThread());

  FilterBuilder builder;
  for (ScopedVector<ManagedModeSiteList>::iterator it = site_lists.begin();
       it != site_lists.end(); ++it) {
    builder.AddSiteList(*it);
  }

  return builder.Build();
}

}  // namespace

ManagedModeURLFilter::ManagedModeURLFilter()
    : default_behavior_(ALLOW),
      contents_(new Contents()) {
  // Detach from the current thread so we can be constructed on a different
  // thread than the one where we're used.
  DetachFromThread();
}

ManagedModeURLFilter::~ManagedModeURLFilter() {
  DCHECK(CalledOnValidThread());
}

// static
ManagedModeURLFilter::FilteringBehavior
ManagedModeURLFilter::BehaviorFromInt(int behavior_value) {
  DCHECK_GE(behavior_value, ALLOW);
  DCHECK_LE(behavior_value, BLOCK);
  return static_cast<FilteringBehavior>(behavior_value);
}

// static
GURL ManagedModeURLFilter::Normalize(const GURL& url) {
  GURL normalized_url = url;
  GURL::Replacements replacements;
  // Strip username, password, query, and ref.
  replacements.ClearUsername();
  replacements.ClearPassword();
  replacements.ClearQuery();
  replacements.ClearRef();
  return url.ReplaceComponents(replacements);
}

// static
bool ManagedModeURLFilter::HasStandardScheme(const GURL& url) {
  for (size_t i = 0; i < arraysize(kStandardSchemes); ++i) {
      if (url.scheme() == kStandardSchemes[i])
        return true;
    }
  return false;
}

std::string GetHostnameHash(const GURL& url) {
  std::string hash = base::SHA1HashString(url.host());
  return base::HexEncode(hash.data(), hash.length());
}

// static
bool ManagedModeURLFilter::HostMatchesPattern(const std::string& host,
                                              const std::string& pattern) {
  std::string trimmed_pattern = pattern;
  std::string trimmed_host = host;
  if (EndsWith(pattern, ".*", true)) {
    size_t registry_length = GetRegistryLength(
        trimmed_host, EXCLUDE_UNKNOWN_REGISTRIES, EXCLUDE_PRIVATE_REGISTRIES);
    // A host without a known registry part does not match.
    if (registry_length == 0)
      return false;

    trimmed_pattern.erase(trimmed_pattern.length() - 2);
    trimmed_host.erase(trimmed_host.length() - (registry_length + 1));
  }

  if (StartsWithASCII(trimmed_pattern, "*.", true)) {
    trimmed_pattern.erase(0, 2);

    // The remaining pattern should be non-empty, and it should not contain
    // further stars. Also the trimmed host needs to end with the trimmed
    // pattern.
    if (trimmed_pattern.empty() ||
        trimmed_pattern.find('*') != std::string::npos ||
        !EndsWith(trimmed_host, trimmed_pattern, true)) {
      return false;
    }

    // The trimmed host needs to have a dot separating the subdomain from the
    // matched pattern piece, unless there is no subdomain.
    int pos = trimmed_host.length() - trimmed_pattern.length();
    DCHECK_GE(pos, 0);
    return (pos == 0) || (trimmed_host[pos - 1] == '.');
  }

  return trimmed_host == trimmed_pattern;
}

ManagedModeURLFilter::FilteringBehavior
ManagedModeURLFilter::GetFilteringBehaviorForURL(const GURL& url) const {
  DCHECK(CalledOnValidThread());

  // URLs with a non-standard scheme (e.g. chrome://) are always allowed.
  if (!HasStandardScheme(url))
    return ALLOW;

  // Check manual overrides for the exact URL.
  std::map<GURL, bool>::const_iterator url_it = url_map_.find(Normalize(url));
  if (url_it != url_map_.end())
    return url_it->second ? ALLOW : BLOCK;

  // Check manual overrides for the hostname.
  std::string host = url.host();
  std::map<std::string, bool>::const_iterator host_it = host_map_.find(host);
  if (host_it != host_map_.end())
    return host_it->second ? ALLOW : BLOCK;

  // Look for patterns matching the hostname, with a value that is different
  // from the default (a value of true in the map meaning allowed).
  for (std::map<std::string, bool>::const_iterator host_it =
      host_map_.begin(); host_it != host_map_.end(); ++host_it) {
    if ((host_it->second == (default_behavior_ == BLOCK)) &&
        HostMatchesPattern(host, host_it->first)) {
      return host_it->second ? ALLOW : BLOCK;
    }
  }

  // If the default behavior is to allow, we don't need to check anything else.
  if (default_behavior_ == ALLOW)
    return ALLOW;

  // Check the list of URL patterns.
  std::set<URLMatcherConditionSet::ID> matching_ids =
      contents_->url_matcher.MatchURL(url);
  if (!matching_ids.empty())
    return ALLOW;

  // Check the list of hostname hashes.
  if (contents_->hash_site_map.count(GetHostnameHash(url)))
    return ALLOW;

  // Fall back to the default behavior.
  return default_behavior_;
}

void ManagedModeURLFilter::GetSites(
    const GURL& url,
    std::vector<ManagedModeSiteList::Site*>* sites) const {
  std::set<URLMatcherConditionSet::ID> matching_ids =
      contents_->url_matcher.MatchURL(url);
  for (std::set<URLMatcherConditionSet::ID>::const_iterator it =
           matching_ids.begin(); it != matching_ids.end(); ++it) {
    std::map<URLMatcherConditionSet::ID, int>::const_iterator entry =
        contents_->matcher_site_map.find(*it);
    if (entry == contents_->matcher_site_map.end()) {
      NOTREACHED();
      continue;
    }
    sites->push_back(&contents_->sites[entry->second]);
  }

  typedef base::hash_multimap<std::string, int>::const_iterator
      hash_site_map_iterator;
  std::pair<hash_site_map_iterator, hash_site_map_iterator> bounds =
      contents_->hash_site_map.equal_range(GetHostnameHash(url));
  for (hash_site_map_iterator hash_it = bounds.first;
       hash_it != bounds.second; hash_it++) {
    sites->push_back(&contents_->sites[hash_it->second]);
  }
}

void ManagedModeURLFilter::SetDefaultFilteringBehavior(
    FilteringBehavior behavior) {
  DCHECK(CalledOnValidThread());
  default_behavior_ = behavior;
}

void ManagedModeURLFilter::LoadWhitelists(
    ScopedVector<ManagedModeSiteList> site_lists) {
  DCHECK(CalledOnValidThread());

  base::PostTaskAndReplyWithResult(
      BrowserThread::GetBlockingPool(),
      FROM_HERE,
      base::Bind(&LoadWhitelistsOnBlockingPoolThread,
                 base::Passed(&site_lists)),
      base::Bind(&ManagedModeURLFilter::SetContents, this));
}

void ManagedModeURLFilter::SetFromPatterns(
    const std::vector<std::string>& patterns) {
  DCHECK(CalledOnValidThread());

  base::PostTaskAndReplyWithResult(
      BrowserThread::GetBlockingPool(),
      FROM_HERE,
      base::Bind(&CreateWhitelistFromPatterns, patterns),
      base::Bind(&ManagedModeURLFilter::SetContents, this));
}

void ManagedModeURLFilter::SetManualHosts(
    const std::map<std::string, bool>* host_map) {
  DCHECK(CalledOnValidThread());
  host_map_ = *host_map;
  UMA_HISTOGRAM_CUSTOM_COUNTS("ManagedMode.ManualHostsEntries",
                              host_map->size(), 1, 1000, 50);
}

void ManagedModeURLFilter::SetManualURLs(
    const std::map<GURL, bool>* url_map) {
  DCHECK(CalledOnValidThread());
  url_map_ = *url_map;
  UMA_HISTOGRAM_CUSTOM_COUNTS("ManagedMode.ManualURLsEntries",
                              url_map->size(), 1, 1000, 50);
}

void ManagedModeURLFilter::AddObserver(Observer* observer) {
  observers_.AddObserver(observer);
}

void ManagedModeURLFilter::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
}

void ManagedModeURLFilter::SetContents(scoped_ptr<Contents> contents) {
  DCHECK(CalledOnValidThread());
  contents_ = contents.Pass();
  FOR_EACH_OBSERVER(Observer, observers_, OnSiteListUpdated());
}
