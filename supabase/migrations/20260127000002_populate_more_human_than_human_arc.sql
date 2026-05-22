-- ============================================================================
-- MORE HUMAN THAN HUMAN: Populate Arc and Seasons
-- ============================================================================
-- A Three-Season Inquiry Arc on Humanity, Detection, and Refusal
-- Custodian: Inquiry Institute | Lead Faculty: a.turing
-- ============================================================================

-- Insert the arc
INSERT INTO inquiry_arcs (
    slug,
    title,
    subtitle,
    core_question,
    thesis,
    custodian,
    lead_faculty_slug,
    guiding_sentence,
    pedagogical_model,
    ethics_safeguards,
    credential_title,
    credential_description,
    published
) VALUES (
    'more-human-than-human',
    'More Human Than Human',
    'A Three-Season Inquiry Arc on Humanity, Detection, and Refusal',
    'What does it mean to be human when humanity is tested, detected, scored, and enforced by systems?',
    'This arc treats "humanity detection" not as a technical challenge alone, but as a ritual, an institutional practice, and a moral technology. Students are not trained to bypass systems, but to reconstruct them, interrogate their assumptions, design alternatives, and ultimately question whether such tests should exist at all.',
    'Inquiry Institute',
    'a-turing',
    'This course is not about proving you are human. It is about questioning why anyone is allowed to decide.',
    '{
        "core_methods": [
            "Maieutic examination (Socratic, reflective, non-scoring)",
            "Symposia (6+1) with a formal heretic seat",
            "Salons (informal, drifting discussion)",
            "Debates (structured opposition)",
            "Drama / Improv (ritualized tests, not performance)",
            "Engineering as inquiry, not optimization"
        ],
        "what_this_is_not": [
            "Not credential farming",
            "Not prompt engineering",
            "Not how to fool AI",
            "Not surveillance training"
        ]
    }',
    ARRAY[
        'No live CAPTCHA bypassing',
        'No impersonation of humans',
        'No deception about AI identity',
        'Faculty explicitly test-aware',
        'Clear public framing of intent'
    ],
    'Inquiry into Humanity & Detection',
    'Awarded upon completion of S1 + S2. Not a professional license. Not a compliance credential. A marker of inquiry, not mastery.',
    true
);

-- Get the arc ID for foreign key references
DO $$
DECLARE
    v_arc_id UUID;
BEGIN
    SELECT id INTO v_arc_id FROM inquiry_arcs WHERE slug = 'more-human-than-human';

    -- ========================================================================
    -- SEASON 0: Electric Sheep
    -- ========================================================================
    INSERT INTO arc_seasons (
        arc_id,
        season_number,
        slug,
        title,
        subtitle,
        enrollment_mode,
        enrollment_end,
        duration_weeks,
        description,
        core_texts,
        content_modules,
        faculty_slugs,
        price_usd,
        credit_toward_next,
        assessment_model,
        assessment_config,
        status
    ) VALUES (
        v_arc_id,
        0,
        'electric-sheep',
        'Electric Sheep',
        'An always-open preparatory threshold, not a cohort',
        'open',
        '2026-05-01'::TIMESTAMPTZ,
        NULL,  -- Asynchronous, no fixed duration
        'An always-open preparatory threshold, not a cohort. Asynchronous, no exams, optional participation, reflective not technical.',
        ARRAY['Do Androids Dream of Electric Sheep? by Philip K. Dick'],
        '[
            {
                "number": 1,
                "title": "Humanity as a Testable Property",
                "description": "What does it mean to claim humanity can be measured, verified, or proven?",
                "type": "reflection"
            },
            {
                "number": 2,
                "title": "Early Empathy and Intelligence Tests",
                "description": "The history of testing: IQ tests, empathy measures, and their institutional uses.",
                "type": "historical"
            },
            {
                "number": 3,
                "title": "CAPTCHA and Cultural Lag",
                "description": "How verification systems encode assumptions about human capability.",
                "type": "technical"
            },
            {
                "number": 4,
                "title": "Behavioral Scoring",
                "description": "From credit scores to social credit: when behavior becomes data.",
                "type": "sociological"
            },
            {
                "number": 5,
                "title": "The Penfield Mood Organ",
                "description": "Affective technology and the engineering of emotional states.",
                "type": "philosophical"
            },
            {
                "number": 6,
                "title": "The Voight-Kampff Ritual",
                "description": "The test as ceremony: what the Voight-Kampff reveals about its administrators.",
                "type": "synthesis"
            }
        ]'::JSONB,
        ARRAY['a-dick', 'a-turing', 'a-weizenbaum'],
        300.00,
        true,  -- Credit applies toward Season 1
        'reflection_only',
        '{
            "grades": false,
            "optional_artifact": {
                "name": "Voight-Kampff Field Note",
                "type": "reflective essay",
                "graded": false
            },
            "drama_component": {
                "name": "Improvised Voight-Kampff Scene",
                "optional": true,
                "recorded": true,
                "pressure": "none"
            }
        }'::JSONB,
        'enrolling'
    );

    -- ========================================================================
    -- SEASON 1: Voight-Kampff
    -- ========================================================================
    INSERT INTO arc_seasons (
        arc_id,
        season_number,
        slug,
        title,
        subtitle,
        enrollment_mode,
        run_start,
        run_end,
        duration_weeks,
        description,
        core_texts,
        content_modules,
        faculty_slugs,
        price_usd,
        assessment_model,
        assessment_config,
        github_classroom_url,
        status,
        enrollment_cap
    ) VALUES (
        v_arc_id,
        1,
        'voight-kampff',
        'Voight-Kampff',
        'Design and formalization of a humanity test',
        'cohort',
        '2026-02-01'::TIMESTAMPTZ,
        '2026-05-01'::TIMESTAMPTZ,
        12,
        'Students reconstruct existing humanity detection systems using synthetic data, simulated users, and transparent assumptions. They implement systems as claimed to work, not as defeated in the wild.',
        ARRAY[
            'Do Androids Dream of Electric Sheep? by Philip K. Dick',
            'Computer Power and Human Reason by Joseph Weizenbaum'
        ],
        '[
            {
                "title": "Text CAPTCHAs",
                "type": "technical_implementation",
                "description": "Reconstruct text-based challenge-response systems"
            },
            {
                "title": "Image Classification CAPTCHAs",
                "type": "technical_implementation",
                "description": "Build image-based verification with transparent assumptions"
            },
            {
                "title": "Behavioral Timing Analysis",
                "type": "technical_implementation",
                "description": "Implement keystroke dynamics and interaction patterns"
            },
            {
                "title": "Risk-Based Scoring",
                "type": "technical_implementation",
                "description": "Design composite risk scores from behavioral signals"
            },
            {
                "title": "Proof-of-Work Challenges",
                "type": "technical_implementation",
                "description": "Computational challenges as humanity proxies"
            },
            {
                "title": "Knowledge-Based Tests",
                "type": "technical_implementation",
                "description": "Cultural knowledge as verification mechanism"
            },
            {
                "title": "Composite Systems",
                "type": "capstone",
                "description": "Design an integrated humanity detection system"
            }
        ]'::JSONB,
        ARRAY['a-turing', 'a-shannon', 'a-weizenbaum', 'a-foucault-soci'],
        1200.00,
        'pass_revise_refuse',
        '{
            "examination_type": "maieutic",
            "requirements": [
                "Explain what the test assumes",
                "Demonstrate who it fails",
                "Articulate harm",
                "Defend or renounce the test"
            ],
            "numeric_grades": false,
            "programming_framework": {
                "languages": ["Python", "JavaScript"],
                "emphasis": "documentation over performance",
                "rule_based_baselines": true
            },
            "github_classroom": {
                "use_for": ["code submissions", "peer review", "versioned reflection"],
                "student_records_stored": false
            }
        }'::JSONB,
        NULL,  -- To be set when GitHub Classroom is configured
        'announced',
        30
    );

    -- ========================================================================
    -- SEASON 2: More Human Than Human
    -- ========================================================================
    INSERT INTO arc_seasons (
        arc_id,
        season_number,
        slug,
        title,
        subtitle,
        enrollment_mode,
        run_start,
        run_end,
        duration_weeks,
        description,
        core_texts,
        content_modules,
        faculty_slugs,
        price_usd,
        assessment_model,
        assessment_config,
        status,
        enrollment_cap
    ) VALUES (
        v_arc_id,
        2,
        'more-human-than-human',
        'More Human Than Human',
        'Evaluation, collapse, refusal',
        'cohort',
        '2026-08-01'::TIMESTAMPTZ,
        '2026-11-01'::TIMESTAMPTZ,
        12,
        'If a test cannot be trusted, what replaces it? This season explores detector breakdown scenarios, role-reversal examinations, refusal protocols, and institutional critique.',
        ARRAY[
            'Blade Runner (film)',
            'Blade Runner 2049 (film)',
            'Pale Fire by Vladimir Nabokov (optional)'
        ],
        '[
            {
                "title": "Test-Aware Faculty Interrogation",
                "type": "dramatic",
                "description": "Faculty who know they are being tested"
            },
            {
                "title": "Detector Breakdown Scenarios",
                "type": "technical",
                "description": "When detection systems fail catastrophically"
            },
            {
                "title": "Role-Reversal Examinations",
                "type": "dramatic",
                "description": "Students as examiners, faculty as subjects"
            },
            {
                "title": "Refusal Protocols",
                "type": "philosophical",
                "description": "Designing systems that decline to classify"
            },
            {
                "title": "Institutional Critique",
                "type": "synthesis",
                "description": "Who benefits from humanity testing?"
            }
        ]'::JSONB,
        ARRAY['a-turing', 'a-dick', 'a-arendt', 'a-foucault-humn', 'a-nabokov'],
        1200.00,
        'pass_revise_refuse',
        '{
            "final_examination_options": [
                "Design a refusal protocol",
                "Design a non-test-based judgment system",
                "Write a critique demonstrating impossibility of fair testing"
            ],
            "central_question": "If a test cannot be trusted, what replaces it?",
            "silence_permitted": true,
            "refusal_valid": true
        }'::JSONB,
        'announced',
        30
    );

    -- ========================================================================
    -- FACULTY ASSIGNMENTS
    -- ========================================================================
    
    -- Lead Faculty (arc-wide)
    INSERT INTO arc_faculty (arc_id, faculty_slug, role, focus_area)
    VALUES (v_arc_id, 'a-turing', 'lead', 'Tests, limits, and the logic of detection');

    -- Core Faculty (arc-wide)
    INSERT INTO arc_faculty (arc_id, faculty_slug, role, focus_area) VALUES
    (v_arc_id, 'a-dick', 'core', 'Artificial persons, empathy, breakdown'),
    (v_arc_id, 'a-weizenbaum', 'core', 'Refusal, moral limits of computation'),
    (v_arc_id, 'a-arendt', 'core', 'Judgment, banality, responsibility'),
    (v_arc_id, 'a-shannon', 'core', 'Information vs meaning'),
    (v_arc_id, 'a-foucault-soci', 'core', 'Classification, power, surveillance');

    -- Optional Faculty
    INSERT INTO arc_faculty (arc_id, faculty_slug, role, focus_area)
    VALUES (v_arc_id, 'a-nabokov', 'guest', 'Interpretation, misreading, Pale Fire');

END $$;

-- ============================================================================
-- VERIFICATION
-- ============================================================================
DO $$
DECLARE
    v_arc_count INTEGER;
    v_season_count INTEGER;
    v_faculty_count INTEGER;
BEGIN
    SELECT COUNT(*) INTO v_arc_count FROM inquiry_arcs WHERE slug = 'more-human-than-human';
    SELECT COUNT(*) INTO v_season_count FROM arc_seasons WHERE arc_id = (SELECT id FROM inquiry_arcs WHERE slug = 'more-human-than-human');
    SELECT COUNT(*) INTO v_faculty_count FROM arc_faculty WHERE arc_id = (SELECT id FROM inquiry_arcs WHERE slug = 'more-human-than-human');
    
    RAISE NOTICE 'More Human Than Human arc created: % arc, % seasons, % faculty assignments', v_arc_count, v_season_count, v_faculty_count;
END $$;
