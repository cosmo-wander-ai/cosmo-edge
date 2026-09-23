import unittest

from tools.vlm_eval.description import make_scores, strip_grade


class DescriptionTests(unittest.TestCase):
    def test_grade_removal_keeps_pronoun_and_scene(self):
        self.assertEqual(strip_grade('Safety level: IV. A worker stands here.'), 'A worker stands here')
        self.assertEqual(strip_grade('[II] I see a worker.'), 'I see a worker')
        self.assertEqual(strip_grade('A type IV cylinder.'), 'A type IV cylinder')

    def test_real_label_templates_removed_without_hazard_loss(self):
        for label in ('The safety level is grade two', 'The safety level is Grade 2',
                      'The safety level is grade one', 'The safety level is Grade II',
                      'The safety level is 3', 'Risk level: four'):
            self.assertEqual(strip_grade(label + '. A fire burns near fuel.'), 'A fire burns near fuel')
            self.assertEqual(strip_grade('A fire burns near fuel. ' + label + '.'), 'A fire burns near fuel')
        self.assertEqual(strip_grade('Two workers stand beside four fuel drums.'),
                         'Two workers stand beside four fuel drums')

    def test_scoring_failure_preserved(self):
        def broken(texts):
            raise RuntimeError('constructed scorer failure')
        scores = make_scores([dict(id='a', safety_level='IV', description='A scene')],
                             [dict(id='a', status='ok', raw_output='{"description":"A scene","safety_level":"IV"}')], broken)
        self.assertEqual(scores['a']['status'], 'scoring_error')
        self.assertNotIn('cosine', scores['a'])

    def test_failed_execution_not_encoded(self):
        def forbidden(texts):
            self.fail('failed execution must not be encoded')
        self.assertEqual(make_scores([dict(id='a', safety_level='IV', description='A scene')],
                                     [dict(id='a', status='timeout', raw_output='')], forbidden), {})


if __name__ == '__main__':
    unittest.main()
