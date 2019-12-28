#include "model.h"

#include <fst/fst.h>
#include <fst/register.h>
#include <fst/matcher-fst.h>
#include <fst/extensions/ngram/ngram-fst.h>

namespace fst {

static FstRegisterer<StdOLabelLookAheadFst>
    OLabelLookAheadFst_StdArc_registerer;

static FstRegisterer<MatcherFst<
    ConstFst<LogArc>,
    LabelLookAheadMatcher<SortedMatcher<ConstFst<LogArc>>,
                          olabel_lookahead_flags, FastLogAccumulator<LogArc>>,
    olabel_lookahead_fst_type, LabelLookAheadRelabeler<LogArc>>>
    OLabelLookAheadFst_LogArc_registerer;

static FstRegisterer<NGramFst<StdArc>> NGramFst_StdArc_registerer;
}  // namespace fst

#ifdef __ANDROID__
#include <android/log.h>
static void AndroidLogHandler(const LogMessageEnvelope &env, const char *message)
{
    __android_log_print(ANDROID_LOG_VERBOSE, "KaldiDemo", message, 1);
}
#endif

Model::Model(const char *model_path) {

#ifdef __ANDROID__
    SetLogHandler(AndroidLogHandler);
#endif

    const char *usage = "Read the docs";
    const char *extra_args[] = {
        "--min-active=200",
        "--max-active=3000",
        "--beam=10.0",
        "--lattice-beam=2.0",
        "--acoustic-scale=1.0",

        "--frame-subsampling-factor=3",

        "--endpoint.silence-phones=1:2:3:4:5:6:7:8:9:10",
        "--endpoint.rule2.min-trailing-silence=0.5",
        "--endpoint.rule3.min-trailing-silence=1.0",
        "--endpoint.rule4.min-trailing-silence=2.0",
    };
    std::string model_path_str(model_path);

    kaldi::ParseOptions po(usage);
    nnet3_decoding_config_.Register(&po);
    endpoint_config_.Register(&po);
    decodable_opts_.Register(&po);

    std::vector<const char*> args;
    args.push_back("server");
    args.insert(args.end(), extra_args, extra_args + sizeof(extra_args) / sizeof(extra_args[0]));
    po.Read(args.size(), args.data());

    feature_info_.feature_type = "mfcc";
    ReadConfigFromFile(model_path_str + "/mfcc.conf", &feature_info_.mfcc_opts);

    feature_info_.silence_weighting_config.silence_weight = 1e-3;
    feature_info_.silence_weighting_config.silence_phones_str = "1:2:3:4:5:6:7:8:9:10";

    OnlineIvectorExtractionConfig ivector_extraction_opts;
    ivector_extraction_opts.splice_config_rxfilename = model_path_str + "/ivector/splice.conf";
    ivector_extraction_opts.cmvn_config_rxfilename = model_path_str + "/ivector/online_cmvn.conf";
    ivector_extraction_opts.lda_mat_rxfilename = model_path_str + "/ivector/final.mat";
    ivector_extraction_opts.global_cmvn_stats_rxfilename = model_path_str + "/ivector/global_cmvn.stats";
    ivector_extraction_opts.diag_ubm_rxfilename = model_path_str + "/ivector/final.dubm";
    ivector_extraction_opts.ivector_extractor_rxfilename = model_path_str + "/ivector/final.ie";
    ivector_extraction_opts.num_gselect = 5;
    ivector_extraction_opts.min_post = 0.025;
    ivector_extraction_opts.posterior_scale = 0.1;
    ivector_extraction_opts.max_remembered_frames = 1000;
    ivector_extraction_opts.max_count = 100;
    ivector_extraction_opts.ivector_period = 200;
    feature_info_.use_ivectors = true;
    feature_info_.ivector_extractor_info.Init(ivector_extraction_opts);

    nnet3_rxfilename_ = model_path_str + "/final.mdl";
    hcl_fst_rxfilename_ = model_path_str + "/HCLr.fst";
    g_fst_rxfilename_ = model_path_str + "/Gr.fst";
    disambig_rxfilename_ = model_path_str + "/disambig_tid.int";

    trans_model_ = new kaldi::TransitionModel();
    nnet_ = new kaldi::nnet3::AmNnetSimple();
    {
        bool binary;
        kaldi::Input ki(nnet3_rxfilename_, &binary);
        trans_model_->Read(ki.Stream(), binary);
        nnet_->Read(ki.Stream(), binary);
        SetBatchnormTestMode(true, &(nnet_->GetNnet()));
        SetDropoutTestMode(true, &(nnet_->GetNnet()));
        nnet3::CollapseModel(nnet3::CollapseModelConfig(), &(nnet_->GetNnet()));
    }

    decodable_info_ = new nnet3::DecodableNnetSimpleLoopedInfo(decodable_opts_,
                                                               nnet_);
    hcl_fst_ = fst::StdFst::Read(hcl_fst_rxfilename_);
    g_fst_ = fst::StdFst::Read(g_fst_rxfilename_);

    word_syms_ = g_fst_->OutputSymbols();
    if (word_syms_ == NULL)
        KALDI_ERR << "No word symbols in the grammar";

    kaldi::WordBoundaryInfoNewOpts opts;
    winfo_ = new kaldi::WordBoundaryInfo(opts, model_path_str + "/word_boundary.int");

    ReadIntegerVectorSimple(disambig_rxfilename_, &disambig_);
}

Model::~Model() {
    delete decodable_info_;
    delete trans_model_;
    delete nnet_;
    delete winfo_;
    delete hcl_fst_;
    delete g_fst_;
}
