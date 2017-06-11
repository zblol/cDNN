import argparse
import codecs
import collections
import json
import os
import shutil
import subprocess
from datetime import datetime

import utils


if __name__ == "__main__":
    parser = argparse.ArgumentParser()

    parser.add_argument("trainer_json")

    args = parser.parse_args()

    conf = json.load(open(args.trainer_json),
                     object_pairs_hook=collections.OrderedDict)

    # check train file
    if not os.path.exists(conf['train_file']):
        raise IOError("train file not found.")
    # check eval file
    if not os.path.exists(conf['eval_file']):
        raise IOError("eval file not found.")
    # check pre emb
    if conf['pre_emb'] != '0' and not os.path.exists(conf['pre_emb']):
        raise IOError("pre-trained embedding not found.")
    # check model dir
    if not os.path.exists(conf['model_dir']) or \
            not os.path.isdir(conf['model_dir']):
        raise IOError("model dir not found")
    # check conll scorer
    if not os.path.exists(conf['conll_scorer']):
        raise IOError("conll scorer not found.")

    # validate bio
    utils.bio_validator(conf['train_file'])
    utils.bio_validator(conf['eval_file'])

    # check pre-emb dimension
    if conf['pre_emb'] != '0':
        f = codecs.open(conf['pre_emb'])
        first_line = f.readline()
        second_line = f.readline()
        pre_emb_dim = len(second_line.split()) - 1
        if int(conf['word_dim']) != pre_emb_dim:
            raise ValueError(
                "pre-trained embedding size and word dimension not match"
            )

    # create a unique model id by datetime
    date = datetime.date(datetime.now())
    time = datetime.time(datetime.now())
    model_id = '_'.join([str(item) for item in
                         [date.year, date.month, date.day,
                          time.hour, time.minute, time.second,
                          time.microsecond]
                         ])
    conf['model_dir'] = os.path.join(conf['model_dir'], model_id)
    os.mkdir(conf['model_dir'])

    # copy trainer config json to model dir
    print("=> net configuration")
    shutil.copy(args.trainer_json, conf['model_dir'])

    # print net configuration
    for key, value in conf.items():
        print("%s = %s" % (key, value))

    # executing trainer
    trainer_exe = os.path.join(os.path.dirname(os.path.abspath("__file__")),
                               "../bin/seqLabelingTrainer")
    cmd = [
        trainer_exe,
        conf['train_file'],
        conf['eval_file'],
        conf['model_dir'],
        conf['pre_emb'],
        conf['word_dim'],
        conf['char_dim'],
        conf['word_lstm_dim'],
        conf['char_lstm_dim'],
        conf['learning_rate'],
        conf['dropout'],
        conf['all_emb'],
        conf['num_thread'],
        conf['conll_scorer'],
    ]
    print('=>executing trainer...')
    print(' '.join(cmd))

    subprocess.call(cmd)



