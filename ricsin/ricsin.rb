require 'optparse'
require 'pp'

class Ricsin
  VERSION = '0.0.1'
  VERSION_STRING = "ricsin #{VERSION}"

  def self.generate file, option
    ricsin = self.new(file, option)
    ricsin.generate
    ricsin.run if option[:run]
  end

  def initialize file, option
    @basename = File.basename(file, '.rcb')
    @output_dir = option[:output_dir]
    @src_rcb = file
    @out_rb = File.join(@output_dir, "ricsin_#{@basename}.rb")
    @out_c  = File.join(@output_dir, "ricsin_#{@basename}.c")
    @out_so = nil
    @so_basename = "ricsin_#{@basename}"
    @src = nil
    @option = option
    @ids = {}
  end

  def run
    raise unless @out_so
    require @out_rb
  end

  def generate
    @src = preprocess(@src_rcb)
    File.open(@out_rb, 'w'){|f|
      f.write @src
    } if @option[:output_rb]

    generate_csrc
    generate_dll
    generate_rb
  end

  def generate_rb
    open(@out_rb, 'wb'){|f|
      f.puts "require_relative '#{@so_basename}.so'"
      f.puts "$0 = '#{@basename}.rcb' if $0 == __FILE__"
      f.puts "::RICSIN_ISEQMAP['#{@so_basename}'].eval"
    }
  end

  def preprocess file
    # #C cexpr => __Ccont__ %q{ cexpr }, 0
    # #C:10 cexpr => __Ccont__ %q{ cexpr }, 10

    # kill __END__ lines
    src = File.read(file).sub(/^__END__.+/m, '')

    src = src.gsub(/^(\s*\#C\s+(.+)(?:\n|\z))+/){|lines|
      csrc = lines.strip.split(/[\n]+/).map{|line|
        if /^\s*\#C\s+(.+)\s*$/ =~ line
          $1.chomp.dump
        else
          raise line.dump
        end
      }.join("\\\n")
      "__Ccont__(#{csrc}, 0)\n"
    }
    # .gsub(/^\s*=begin C\s*$(.+)^\s*=end\s*$/m){
    #  #/^\s*=begin C$.+^\s*=end$/m
    #  "__C__ <<'EOS__'\n#{$1.strip}\nEOS__"
    #}

    src
  end

  def generate_dll
    require 'rbconfig'
    ruby = File.join(
      RbConfig::CONFIG["bindir"],
      RbConfig::CONFIG["ruby_install_name"])

    Dir.chdir(@output_dir){
      if FileTest.exist? 'extconf.rb'
        system("#{ruby} extconf.rb")
      else
        cmd = "#{ruby} -r mkmf -e "\
              "'$objs=[#{(File.basename(@out_c, '.c')+'.o').dump}]; " \
              "create_makefile(#{@so_basename.dump})'"
        system(cmd)
      end or raise "extconf"
      system("make") or raise "make"
      @out_so = File.expand_path("#{@so_basename}.#{RbConfig::CONFIG['DLEXT']}")
    }
  end

  Str = /("[^\\"]*(?:\\.[^\\"]*)*")/m
  Com = /(\/\*.*?\*\/)/m
  Oth = /([\w\W]+?(?=[\"\/]|\z))/m
  CProg = /\G(?:#{Str}|#{Com}|#{Oth})/m

  def preprocess_c src
    ret = ''
    src.scan(CProg){|str, comm, body|
      ret << str if str
      ret << comm if comm
      ret << body.gsub(/\$(\w+)\b/){
        # @ids[$~.to_s] = $1
        "RGV(#{$1})"
      }.gsub(/\@(\w+)\b/){
        @ids[$~.to_s] = $1
        "RIV(#{$1})"
      } if body
    }
    ret
  end

  def csrc_function name, params, src, pre_csrc, fastcall = true
    # self
    if /\bself\b/ =~ src
      self_decl = "const VALUE self = RICSIN_RUBY_SELF();"
    else
      self_decl = ''
    end

<<EOS
static VALUE
#{fastcall ? "FUNC_FASTCALL(#{name})" : name}(#{params})
{
  #{self_decl}
  #{pre_csrc}
{
#{src}
}
  return Qnil;
}
EOS
   end

   def csrc_init_function
<<EOS
/* #line #{__LINE__} "#{__FILE__}" */

static const rb_insn_func_t ricsin_functions[] = {
  #{@funcs.join(",\n  ")}
};

static const char ruby_source[] = {
#{src = ''
  @src.each_byte.each_slice(10){|e|
    src << '  ' + e.map{|e| '0x%02x, ' % e}.join << "\n"
  }; src
}
};

VALUE rb_iseq_compile_with_option(VALUE src, VALUE file, VALUE line, VALUE opt);

void
Init_#{@so_basename}(void)
{
  VALUE src = rb_str_new(ruby_source, sizeof(ruby_source));
  VALUE file = rb_str_new2("#{File.basename(@src_rcb)}");
  VALUE line = INT2FIX(1);
  VALUE opt = rb_hash_new();
  VALUE map;
  VALUE iseq;

  #{
    $ricsin_init_source.map{|e|
      "{#{e}}"
    }.join("\n")
  }

  #{
    @ids.map{|(k, v)|
      "__ricsin_id_#{v} = rb_intern(\"#{k}\");"
    }.join("\n")
  }

  rb_hash_aset(opt, ID2SYM(rb_intern("ricsin_mode")), INT2FIX(2) /* exec mode */);
  rb_hash_aset(opt, ID2SYM(rb_intern("ricsin_funcptrs")), (VALUE)ricsin_functions | 0x01);

  iseq = rb_iseq_compile_with_option(src, file, line, opt);

  if (0) {
    VALUE disasm_str = ruby_iseq_disasm(iseq);
    rb_io_puts(1, &disasm_str, rb_stdout);
  }

  if (rb_const_defined(rb_cObject, rb_intern("RICSIN_ISEQMAP"))) {
    map = rb_const_get(rb_cObject, rb_intern("RICSIN_ISEQMAP"));
  }
  else {
    map = rb_hash_new();
    rb_const_set(rb_cObject, rb_intern("RICSIN_ISEQMAP"), map);
  }
  rb_hash_aset(map, rb_str_new2("#{@so_basename}"), iseq);
}
EOS
  end

  def generate_func f, lvtbl, dvtbl, funcname, csrc,
                    pre_csrc, blockcall = false
    f.puts
    f.puts
    undefs = []

    # write header

    lvtbl.each{|(v, i)|
      if /[^\d\w]/ !~ v
        if true
          f.puts "#define #{v} RICSIN_RUBY_VAR__#{v}"
        end
        undefs << v
        f.puts "#define RICSIN_RUBY_VAR__#{v} RICSIN_RUBY_LVAR_ACCESS(#{i})"
        undefs << "RICSIN_RUBY_VAR__#{v}"
      end
    }

    dvtbl.each_with_index{|(v, lev, i)|
      if /[^\d\w]/ !~ v
        if true
          f.puts "#define #{v} RICSIN_RUBY_VAR__#{v}"
          undefs << v
        end
        f.puts "#define RICSIN_RUBY_VAR__#{v} RICSIN_RUBY_DVAR_ACCESS(#{lev}, #{i})"
        undefs << "RICSIN_RUBY_VAR__#{v}"
      end
    }

    # write body
    if blockcall
      params = 'VALUE arg, VALUE tval, int argc, VALUE *argv, VALUE blockarg, rb_control_frame_t *__cfp__'
      f.puts csrc_function(funcname, params, csrc, pre_csrc, false)
    else
      params = "rb_control_frame_t *__cfp__"
      f.puts csrc_function(funcname, params, csrc, pre_csrc)
    end

    # write hooter
    undefs.each{|e|
      f.puts "#undef #{e}"
    }
  end

  def generate_csrc
    $ricsin_decl_source = []
    $ricsin_init_source = []
    $ricsin_sources = []

    iseq = RubyVM::InstructionSequence.compile(@src, @src_rcb, 1, {
      :ricsin_mode => 1 # compile mode
    })

    # puts iseq.disasm
    index = 0
    @cx_info = {} # "[xid, iseqid] => [info...]"
    @funcs = []
    func_info = []

    $ricsin_sources.each{|srcinfo|
      @undefs = []
      lvtbl, dvtbl, line, csrc, xid, iseqid, npc = *srcinfo

      csrc.replace preprocess_c(csrc)

      if npc
        if @cx_info[[xid, iseqid]]
          fn, ary = *@cx_info[[xid, iseqid]]
          ary << [line, csrc, npc]
        else
          fn = "ricsin_func_#{index+=1}"
          func_info << ary = [fn, srcinfo, [line, csrc, npc]]
          @cx_info[[xid, iseqid]] = [fn, ary]
        end
      else
        fn = "ricsin_func_#{index+=1}"
        func_info << [fn, srcinfo]
      end

      @funcs << fn
    }

    open(@out_c, 'w'){|f|
      # header
      f.puts "/* auto generated C source code by Ricsin */"
      # f.puts "#include \"../ricsin/ricsin.h\""
      f.puts DATA.read
      f.puts
      f.puts $ricsin_decl_source.join("\n")
      f.puts
      f.puts @ids.map{|(k, v)|
        "static ID __ricsin_id_#{v};"
      }.join("\n")
      f.puts

      func_info.each{|(fn, srcinfo, *rest)|
        lvtbl, dvtbl, line, csrc, xid, iseqid, npc = *srcinfo
        if npc && rest.size > 1
          npcs = []
          csrc = rest.map{|(line, src, npc)|
            npcs << npc
            "/* #line #{line} \"#{@src_rcb}\" */\n" \
            "RICSIN__label_#{npc}:; #{src}; " \
            "RICSIN_RUBY_SET_PC(#{npc}); return Qnil;"
          }.join("\n")

          pre = "switch (RICSIN_RUBY_GET_PC()) {\n" + npcs.map{|n|
            "case #{n}: goto RICSIN__label_#{n};"
          }.join("\n") + "}\n"
          generate_func(f, lvtbl, dvtbl, fn, csrc, pre)
        else
          csrc = "/* #line #{line} \"#{@src_rcb}\" */\n#{csrc}"

          if xid == :ifunc
            # arg, ifunc->nd_tval, argc, argv, blockarg
            generate_func(f, lvtbl, dvtbl, fn, csrc, '', true)
          else
            generate_func(f, lvtbl, dvtbl, fn, csrc, '')
          end
        end
      }

      f.puts csrc_init_function
    }
  end
end

#####################################################################

option = {
  :output_dir => '.',
  :suffix => '',
  :run => false,
  :output_rb => true,
}

opt = OptionParser.new{|o|
  o.on("-C", "--directory [DIR]", 'output directory'){|dir|
    option[:output_dir] = dir
  }
  o.on("-s", "--suffix [SUFFIX]", 'specify output suffix'){|s|
    option[:suffix] = s
  }
  o.on("-r", "--run", 'run generated dll'){|r|
    option[:run] = true
  }
  o.on("--save-ruby-file", 'generate preoprocessed ruby source code'){|g|
    option[:output_rb] = true
  }

  o.on("-v", "--version", 'show version'){
    puts Ricsin::VERSION_STRING
    exit
  }

  o.on("-h", "--help", 'show this message'){
    puts Ricsin::VERSION_STRING
    puts
    puts o
    exit
  }
}

opt.parse!(ARGV)

if $0 == __FILE__
  file = ARGV.shift
  if /rcb$/ !~ file
    raise "Input file of ricsin should have 'rcb' extension: #{file}"
  else
    option[:output_dir] = File.dirname(file)
    Ricsin.generate file, option
  end
end

__END__
/* ricsin.h */

#ifndef RICSIN_H
#define RICSIN_H 1
#include <ruby.h>

typedef struct rb_iseq_struct {
    VALUE ricsin_type;          /* instruction sequence type */
    VALUE ricsin_name;	         /* String: iseq name */
    VALUE ricsin_filename;      /* file information where this sequence from */
    VALUE *ricsin_iseq;         /* iseq (insn number and openrads) */
    VALUE *ricsin_iseq_encoded; /* encoded iseq */
    VALUE dmy[0x10];
} rb_iseq_t;

typedef struct {
    VALUE *ricsin_pc;			/* cfp[0] */
    VALUE *ricsin_sp;			/* cfp[1] */
    VALUE *ricsin_bp;			/* cfp[2] */
    rb_iseq_t *ricsin_iseq;		/* cfp[3] */
    VALUE ricsin_flag;			/* cfp[4] */
    VALUE ricsin_self;			/* cfp[5] / block[0] */
    VALUE *ricsin_lfp;			/* cfp[6] / block[1] */
    VALUE *ricsin_dfp;			/* cfp[7] / block[2] */
    rb_iseq_t *ricsin_block_iseq;	/* cfp[8] / block[3] */
    VALUE ricsin_proc;			/* cfp[9] / block[4] */
    ID ricsin_method_id;               /* cfp[10] saved in special case */
    VALUE ricsin_method_class;         /* cfp[11] saved in special case */
} rb_control_frame_t;

typedef VALUE
  (FUNC_FASTCALL(*rb_insn_func_t))(rb_control_frame_t *);

#define RICSIN_RUBY_SELF()    (__cfp__->ricsin_self)

#define RICSIN_RUBY_LVAR_ACCESS(idx) (*(__cfp__->ricsin_lfp - idx))
#define RICSIN_RUBY_DVAR_ACCESS(lev, idx) (*ricsin_dvar_ptr(__cfp__, lev, idx))
#define RICSIN_RUBY_GET_PC() (__cfp__->ricsin_pc - __cfp__->ricsin_iseq->ricsin_iseq_encoded)
#define RICSIN_RUBY_SET_PC(n) do { \
    __cfp__->ricsin_pc = __cfp__->ricsin_iseq->ricsin_iseq_encoded + (n); \
} while (0)

#define RV(name) RICSIN_RUBY_VAR__##name
#define RIV(name) rb_ivar_get(RICSIN_RUBY_SELF(), __ricsin_id_##name)
#define RGV(name) rb_gv_get(#name)
#define RCV(name) rb_vm_ev_cvar_get(CLASS_OF(RUBY_SELF()), rb_intern("@@" #name)
#define RConst(name) rb_vm_ev_const(__cfp__, rb_intern(#name))
#define RIV_SET(name, val) rv_ivar_set(RICSIN_RUBY_SELF(), __ricsin_id_##name, val)
#define RGV_SET(name, val) rb_gv_set(#name, val)
#define RCV_SET(name, val) rb_vm_ev_cvar_set(__cfp__, rb_intern("@@" #name))

inline static VALUE*
ricsin_dvar_ptr(rb_control_frame_t *cfp, int lev, int idx)
{
    int i;
    VALUE *dfp2 = cfp->ricsin_dfp;

#define RICSIN_GET_PREV_DFP(dfp) ((VALUE *)((dfp)[0] & ~0x03))

    for (i = 0; i < lev; i++) {
	dfp2 = RICSIN_GET_PREV_DFP(dfp2);
    }

    return dfp2 - idx;
}

VALUE ruby_iseq_disasm(VALUE self);
VALUE rb_iseq_eval(VALUE iseqval);
VALUE rb_vm_ev_const(rb_control_frame_t *cfp, ID id);

#endif /* RICSIN_H */

