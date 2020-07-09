#!/usr/bin/env ruby

require 'time'
require 'optparse'

unless Array.instance_methods.include? :to_h
  class Array
    def to_h
      h = {}
      each { |k,v| h[k]=v }
      h
    end
  end
end

# statedump.c:gf_proc_dump_mempool_info uses a five-dash record separator,
# client.c:client_fd_lk_ctx_dump uses a six-dash record separator.
ARRSEP = /^(-{5,6}=-{5,6})?$/
HEAD = /^\[(.*)\]$/
INPUT_FORMATS = %w[statedump json]

format = 'json'
input_format = 'statedump'
tz = '+0000'
memstat_select,memstat_reject = //,/\Z./
OptionParser.new do |op|
  op.banner << " [<] <STATEDUMP>"
  op.on("-f", "--format=F", "json/yaml/memstat(-[plain|human|json])") { |s| format = s }
  op.on("--input-format=F", INPUT_FORMATS.join(?/)) { |s| input_format = s }
  op.on("--timezone=T",
        "time zone to apply to zoneless timestamps [default UTC]") { |s| tz = s }
  op.on("--memstat-select=RX", "memstat: select memory types matching RX") { |s|
    memstat_select = Regexp.new s
  }
  op.on("--memstat-reject=RX", "memstat: reject memory types matching RX") { |s|
    memstat_reject = Regexp.new s
  }
end.parse!


if format =~ /\Amemstat(?:-(.*))?/
  memstat_type = $1 || 'plain'
  unless %w[plain human json].include? memstat_type
    raise "unknown memstat type #{memstat_type.dump}"
  end
  format = 'memstat'
end

repr, logsep = case format
when 'yaml'
  require 'yaml'

  [proc { |e| e.to_yaml }, "\n"]
when 'json', 'memstat'
  require 'json'

  [proc { |e| e.to_json }, " "]
else
  raise "unkonwn format '#{format}'"
end
formatter = proc { |e| puts repr.call(e) }

INPUT_FORMATS.include? input_format or raise "unkwown input format '#{input_format}'"

dumpinfo = {}

# parse a statedump entry
elem_cbk = proc { |s,&cbk|
  arraylike = false
  s.grep(/\S/).empty? and next
  head = nil
  while s.last =~ /^\s*$/
    s.pop
  end
  body = catch { |misc2|
    s[0] =~ HEAD ? (head = $1) : (throw misc2)
    body = [[]]
    s[1..-1].each { |l|
      if l =~ ARRSEP
        arraylike = true
        body << []
        next
      end
      body.last << l
    }

    body.reject(&:empty?).map { |e|
      ea = e.map { |l|
        k,v = l.split("=",2)
        m = /\A(0|-?[1-9]\d*)(\.\d+)?\Z/.match v
        [k, m ? (m[2] ? Float(v) : Integer(v)) : v]
      }
      begin
        ea.to_h
      rescue
        throw misc2
      end
    }
  }

  if body
    cbk.call [head, arraylike ? body : (body.empty? ? {} : body[0])]
  else
    STDERR.puts ["WARNING: failed to parse record:", repr.call(s)].join(logsep)
  end
}

# aggregator routine
aggr = case format
when 'memstat'
  meminfo = {}
  # commit memory-related entries to meminfo
  proc { |k,r|
    case k
    when /memusage/
      (meminfo["GF_MALLOC"]||={})[k] ||= r["size"] if k =~ memstat_select and k !~ memstat_reject
    when "mempool"
      r.each {|e|
        kk = "mempool:#{e['pool-name']}"
        (meminfo["mempool"]||={})[kk] ||= e["size"] if kk =~ memstat_select and kk !~ memstat_reject
      }
    end
  }
else
  # just format data, don't actually aggregate anything
  proc { |pair| formatter.call pair }
end

# processing the data
case input_format
when 'statedump'
  acc = []
  $<.each { |l|
    l = l.strip
    if l =~ /^(DUMP-(?:START|END)-TIME):\s+(.*)/
      dumpinfo["_meta"]||={}
      (dumpinfo["_meta"]["date"]||={})[$1] = Time.parse([$2, tz].join " ")
      next
    end

    if l =~ HEAD
      elem_cbk.call(acc, &aggr)
      acc = [l]
      next
    end

    acc << l
  }
  elem_cbk.call(acc, &aggr)
when 'json'
  $<.each { |l|
    r = JSON.load l
    case r
    when Array
      aggr[r]
    when Hash
      dumpinfo.merge! r
    end
  }
end

# final actions: output aggregated data
case format
when 'memstat'
  ma = meminfo.values.map(&:to_a).inject(:+)
  totals = meminfo.map { |coll,h| [coll, h.values.inject(:+)] }.to_h
  tt = ma.transpose[1].inject(:+)

  summary_sep,showm = case memstat_type
  when 'json'
    ["", proc { |k,v| puts({type: k, value: v}.to_json) }]
  when 'plain', 'human'
    # human-friendly number representation
    hr = proc { |n|
      qa = %w[B kB MB GB]
      q = ((1...qa.size).find {|i| n < (1 << i*10)} || qa.size) - 1
      "%.2f%s" % [n.to_f / (1 << q*10), qa[q]]
    }

    templ = "%{val} %{key}"
    tft = proc { |t| t }
    nft = if memstat_type == 'human'
      nw = [ma.transpose[1], totals.values, tt].flatten.map{|n| hr[n].size}.max
      proc { |n|
        hn = hr[n]
        " " * (nw - hn.size) + hn
      }
    else
      nw = tt.to_s.size
      proc { |n| "%#{nw}d" % n }
    end
    ## Alternative template, key first:
    # templ = "%{key} %{val}"
    # tw = ma.transpose[0].map(&:size).max
    # tft = proc { |t| t + " " * [tw - t.size, 0].max }
    # nft = (memstat_type == 'human') ? hr : proc { |n| n }
    ["\n", proc { |k,v| puts templ % {key: tft[k], val: nft[v]} }]
  else
    raise 'this should be impossible'
  end

  ma.sort_by { |k,v| v }.each(&showm)
  print summary_sep
  totals.each { |coll,t| showm.call "Total #{coll}", t }
  showm.call "TOTAL", tt
else
  formatter.call dumpinfo
end
