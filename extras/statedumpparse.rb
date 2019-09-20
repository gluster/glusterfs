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

format = 'json'
tz = '+0000'
memstat_select,memstat_reject = //,/\Z./
human = false
OptionParser.new do |op|
  op.banner << " [<] <STATEDUMP>"
  op.on("-f", "--format=F", "json/yaml/memstat") { |s| format = s }
  op.on("--timezone=T",
        "time zone to apply to zoneless timestamps [default UTC]") { |s| tz = s }
  op.on("--memstat-select=RX", "memstat: select memory types maxtching RX") { |s|
    memstat_select = Regexp.new s
  }
  op.on("--memstat-reject=RX", "memstat: reject memory types maxtching RX") { |s|
    memstat_reject = Regexp.new s
  }
  op.on("--memstat-human", "memstat: human readable") { human = true }
end.parse!

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

d1 = {}

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
        [k, begin
           Integer v
         rescue ArgumentError
           begin
             Float v
           rescue ArgumentError
             v
           end
         end]
      }
      begin
        ea.to_h
      rescue
        throw misc2
      end
    }
  }

  if body
    cbk.call [head, arraylike ? body : body[0]]
  else
    STDERR.puts ["WARNING: failed to parse record:", repr.call(s)].join(logsep)
  end
}

aggr = case format
when 'memstat'
  mh = {}
  proc { |k,r|
    case k
    when /memusage/
      (mh["GF_MALLOC"]||={})[k] ||= r["size"] if k =~ memstat_select and k !~ memstat_reject
    when "mempool"
      r.each {|e|
        kk = "mempool:#{e['pool-name']}"
        (mh["mempool"]||={})[kk] ||= e["size"] if kk =~ memstat_select and kk !~ memstat_reject
      }
    end
  }
else
  proc { |pair| formatter.call pair }
end

acc = []
$<.each { |l|
  l = l.strip
  if l =~ /^(DUMP-(?:START|END)-TIME):\s+(.*)/
    d1["_meta"]||={}
    (d1["_meta"]["date"]||={})[$1] = Time.parse([$2, tz].join " ")
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

case format
when 'memstat'
  hr = proc { |n|
    qa = %w[B kB MB GB]
    q = ((1...qa.size).find {|i| n < (1 << i*10)} || qa.size) - 1
    [n*100 / (1 << q*10) / 100.0,  qa[q]].join
  }

  ma = mh.values.map(&:to_a).inject(:+)
  totals = mh.map { |coll,h| [coll, h.values.inject(:+)] }.to_h
  tt = ma.transpose[1].inject(:+)

  templ = "%{val} %{key}"
  tft = proc { |t| t }
  nft = if human
    nw = [ma.transpose[1], totals.values, tt].flatten.map{|n| hr[n].size}.max
    proc { |n|
      hn = hr[n]
      " " * (nw - hn.size) + hn
    }
  else
    nw = tt.to_s.size
    proc { |n| "%#{nw}d" % n }
  end
  # templ = "%{key} %{val}"
  # tw = ma.transpose[0].map(&:size).max
  # tft = proc { |t| t + " " * [tw - t.size, 0].max }
  # nft = human ? hr : proc { |n| n }
  showm = proc { |k,v| puts templ % {key: tft[k], val: nft[v]} }

  ma.sort_by { |k,v| v }.each(&showm)
  puts
  totals.each { |coll,t| showm.call "Total #{coll}", t }
  showm.call "TOTAL", tt
else
  formatter.call d1
end
